#pragma once
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// ============================================================
// FileStorage：磁盘文件工具（只负责"文件存哪、怎么流式读写"）。
// 路径约定：<root>/files/<type>/<storage_key前2字符>/<storage_key>
//           <root>/tmp/<transfer_key>.part
// 上传先写 tmp，完成后原子 rename 到正式路径。
// ============================================================
class FileStorage {
public:
    explicit FileStorage(std::string root);
    const std::string& root() const;

    // 由 type + storage_key 生成正式文件路径。
    std::string resolve(const std::string& type, const std::string& storage_key) const;
    // 上传临时文件路径。
    std::string tmp_path(const std::string& basename) const;

    // 流式写：一次只持有一个 chunk 的缓冲，避免整文件进内存。
    class Writer {
    public:
        explicit Writer(std::string path);
        ~Writer();
        bool open();
        bool open_append();  // 以读写方式打开（断点续传/乱序写用，不截断）
        bool write_at(uint64_t offset, const char* data, std::size_t n);  // 定位到 offset 写入
        bool commit(const std::string& final_path); // 关闭并原子 rename 到正式路径
        void abort();
        bool is_open() const { return open_; }
    private:
        std::string   path_;
        std::fstream  fs_;
        bool          open_ = false;
    };

    std::unique_ptr<Writer> create_writer(const std::string& path);

    // 流式读：read() 返回实际读到的字节数，0 表示 EOF。
    class Reader {
    public:
        explicit Reader(std::string path);
        ~Reader();
        bool open();
        bool seek(uint64_t offset);  // 定位到指定偏移（分片下载用）
        std::size_t read(char* buf, std::size_t max);
        bool is_open() const { return open_; }
    private:
        std::string  path_;
        std::ifstream ifs_;
        bool         open_ = false;
    };

    std::unique_ptr<Reader> open_reader(const std::string& path);

    bool remove_file(const std::string& path);

    // 枚举 files/ 下所有正式文件路径（孤儿回收用）。
    std::vector<std::string> list_stored_files() const;

    // 生成 32 位随机 hex，作为 storage_key。
    static std::string generate_storage_key();

private:
    bool ensure_dir(const std::string& dir);

    std::string root_;
};
