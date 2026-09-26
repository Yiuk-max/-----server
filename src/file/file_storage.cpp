#include "file_storage.h"

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <iomanip>
#include <random>
#include <sstream>
#include <sys/stat.h>

namespace {
// 递归创建目录（等价 mkdir -p）。
bool mkdir_p(const std::string& path) {
    if (path.empty()) return true;
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        const std::string sub = path.substr(0, pos);
        if (!sub.empty() && ::mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    if (::mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

// 去掉末尾 '/'，方便拼接路径。
std::string strip_trailing_slash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}
} // namespace

FileStorage::FileStorage(std::string root) : root_(strip_trailing_slash(std::move(root))) {}

const std::string& FileStorage::root() const { return root_; }

std::string FileStorage::resolve(const std::string& type, const std::string& storage_key) const {
    const std::string shard = storage_key.size() >= 2 ? storage_key.substr(0, 2) : "xx";
    return root_ + "/files/" + type + "/" + shard + "/" + storage_key;
}

std::string FileStorage::tmp_path(const std::string& basename) const {
    return root_ + "/tmp/" + basename;
}

bool FileStorage::ensure_dir(const std::string& dir) {
    return mkdir_p(dir);
}

FileStorage::Writer::Writer(std::string path) : path_(std::move(path)) {}

FileStorage::Writer::~Writer() {
    if (open_) fs_.close();
}

bool FileStorage::Writer::open() {
    const std::string dir = path_.substr(0, path_.find_last_of('/'));
    if (!dir.empty() && !mkdir_p(dir)) return false;
    fs_.open(path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    open_ = fs_.is_open();
    return open_;
}

bool FileStorage::Writer::open_append() {
    const std::string dir = path_.substr(0, path_.find_last_of('/'));
    if (!dir.empty() && !mkdir_p(dir)) return false;
    fs_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
    open_ = fs_.is_open();
    return open_;
}

bool FileStorage::Writer::write_at(uint64_t offset, const char* data, std::size_t n) {
    if (!open_ || !fs_.good()) return false;
    fs_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    fs_.write(data, static_cast<std::streamsize>(n));
    return fs_.good();
}

bool FileStorage::Writer::commit(const std::string& final_path) {
    if (open_) {
        fs_.close();
        open_ = false;
    }
    const std::string dir = final_path.substr(0, final_path.find_last_of('/'));
    if (!dir.empty() && !mkdir_p(dir)) return false;
    if (::rename(path_.c_str(), final_path.c_str()) != 0) {
        return false;
    }
    return true;
}

void FileStorage::Writer::abort() {
    if (open_) {
        fs_.close();
        open_ = false;
    }
    ::remove(path_.c_str());
}

std::unique_ptr<FileStorage::Writer> FileStorage::create_writer(const std::string& path) {
    return std::make_unique<Writer>(path);
}

FileStorage::Reader::Reader(std::string path) : path_(std::move(path)) {}

FileStorage::Reader::~Reader() {
    if (open_) ifs_.close();
}

bool FileStorage::Reader::open() {
    ifs_.open(path_, std::ios::binary);
    open_ = ifs_.is_open();
    return open_;
}

bool FileStorage::Reader::seek(uint64_t offset) {
    if (!open_ || !ifs_.good()) return false;
    ifs_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    return ifs_.good();
}

std::size_t FileStorage::Reader::read(char* buf, std::size_t max) {
    if (!open_ || !ifs_.good()) return 0;
    ifs_.read(buf, static_cast<std::streamsize>(max));
    const std::streamsize n = ifs_.gcount();
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

std::unique_ptr<FileStorage::Reader> FileStorage::open_reader(const std::string& path) {
    return std::make_unique<Reader>(path);
}

bool FileStorage::remove_file(const std::string& path) {
    return ::remove(path.c_str()) == 0;
}

std::vector<std::string> FileStorage::list_stored_files() const {
    std::vector<std::string> out;
    const std::string base = root_ + "/files";

    // 递归枚举（POSIX dirent）。
    std::vector<std::string> stack{base};
    while (!stack.empty()) {
        const std::string dir = stack.back();
        stack.pop_back();
        DIR* d = ::opendir(dir.c_str());
        if (!d) continue;
        while (struct dirent* e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            const std::string full = dir + "/" + name;
            if (e->d_type == DT_DIR) {
                stack.push_back(full);
            } else if (e->d_type == DT_REG) {
                out.push_back(full);
            }
        }
        ::closedir(d);
    }
    return out;
}

std::string FileStorage::generate_storage_key() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) {
        oss << std::setw(16) << dist(gen);
    }
    return oss.str(); // 64 位 hex，足够唯一
}
