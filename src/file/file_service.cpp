#include "file_service.h"

#include <iostream>
#include <string>
#include <unordered_set>

#include "repository_hub.h"
#include "file_repository.h"
#include "server_config.h"
#include "sha256.h"

namespace {
// 读取磁盘文件并计算完整 sha256。
std::string hash_file(FileStorage& storage, const std::string& path) {
    auto reader = storage.open_reader(path);
    if (!reader || !reader->open()) return "";
    Sha256 h;
    std::string buf(1 << 20, '\0');
    while (true) {
        const std::size_t n = reader->read(buf.data(), buf.size());
        if (n == 0) break;
        h.update(buf.data(), n);
    }
    return h.final_hex();
}

// ---- 接收位图工具（1 bit / chunk） ----
void mark_received(std::vector<uint8_t>& bm, uint32_t idx) {
    const std::size_t byte = idx / 8;
    if (byte >= bm.size()) bm.resize(byte + 1, 0);
    bm[byte] |= static_cast<uint8_t>(1u << (idx % 8));
}
bool is_received(const std::vector<uint8_t>& bm, uint32_t idx) {
    const std::size_t byte = idx / 8;
    if (byte >= bm.size()) return false;
    return (bm[byte] >> (idx % 8)) & 1u;
}
bool all_received(const std::vector<uint8_t>& bm, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) if (!is_received(bm, i)) return false;
    return true;
}
uint32_t first_missing(const std::vector<uint8_t>& bm, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) if (!is_received(bm, i)) return i;
    return count;
}
std::string bitmap_hex(const std::vector<uint8_t>& bm) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bm.size() * 2);
    for (uint8_t b : bm) { out += hex[b >> 4]; out += hex[b & 0xF]; }
    return out;
}
std::vector<uint8_t> hex_bitmap(const std::string& s, uint32_t chunk_count) {
    std::vector<uint8_t> bm((chunk_count + 7) / 8, 0);
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (std::size_t i = 0; i + 1 < s.size() && i / 2 < bm.size(); i += 2) {
        bm[i / 2] = static_cast<uint8_t>((nib(s[i]) << 4) | nib(s[i + 1]));
    }
    return bm;
}
uint64_t chunk_bytes(uint32_t idx, uint64_t total, uint32_t chunk_size, uint32_t chunk_count) {
    if (idx + 1 < chunk_count) return chunk_size;
    const uint64_t base = static_cast<uint64_t>(chunk_count - 1) * chunk_size;
    return total > base ? total - base : 0;
}
} // namespace

FileService::FileService(std::shared_ptr<RepositoryHub> repo)
    : repo_(std::move(repo)),
      storage_(ServerConfig::get_instance().file_storage_root()),
      chunk_size_(ServerConfig::get_instance().file_chunk_size()),
      max_file_size_(ServerConfig::get_instance().file_max_file_size()),
      max_avatar_size_(ServerConfig::get_instance().file_max_avatar_size()),
      max_storage_size_(ServerConfig::get_instance().file_max_storage_size()) {
    if (chunk_size_ == 0) chunk_size_ = 4 * 1024 * 1024;
}

void FileService::send_system(const std::string& text, const SendFn& send) {
    chat_proto::Envelope env;
    env.set_type("system");
    env.set_content(text);
    send(env, {});
}

void FileService::fill_file_proto(const file_meta_info& f, chat_proto::FileMeta* out) {
    out->set_file_id(std::to_string(f.file_id));
    out->set_original_name(f.original_name);
    out->set_mime_type(f.mime_type);
    out->set_size(f.size);
    out->set_hash(f.hash);
    out->set_type(f.type);
    out->set_uploader_uid(f.uploader_uid);
    out->set_created_at(f.created_at);
}

void FileService::fill_transfer_proto(const file_transfer_info& t, chat_proto::FileTransferInfo* out) {
    out->set_transfer_id(std::to_string(t.transfer_id));
    if (t.file_id >= 0) out->set_file_id(std::to_string(t.file_id));
    out->set_uploader_uid(t.uploader_uid);
    if (t.receiver_uid >= 0) out->set_receiver_uid(t.receiver_uid);
    out->set_direction(t.direction);
    out->set_status(t.status);
    out->set_total_size(t.total_size);
    out->set_transferred_size(t.transferred_size);
    out->set_chunk_size(t.chunk_size);
    out->set_chunk_count(t.chunk_count);
    out->set_next_chunk_index(t.next_chunk_index);
}

// ------------------------------------------------------------
// 上传
// ------------------------------------------------------------
void FileService::upload_init(int uid, const chat_proto::Envelope& msg, const SendFn& send) {
    if (uid < 0) {
        send_system("You must be logged in to upload files.\n", send);
        return;
    }
    const auto& fm = msg.file_meta();
    if (fm.original_name().empty() || fm.size() == 0) {
        send_system("Invalid upload request: original_name and size are required.\n", send);
        return;
    }

    // 白名单校验：type 会参与磁盘路径，防止路径穿越。
    std::string type = fm.type();
    static const std::unordered_set<std::string> kAllowed = {"avatar", "emoji", "image", "attachment", "file"};
    if (kAllowed.find(type) == kAllowed.end()) {
        type = "file";
    }

    // 上传大小审核：先拒绝超限请求，不落盘。
    if (fm.size() > max_file_size_) {
        send_system("Upload rejected: file size exceeds max_file_size.\n", send);
        return;
    }
    if (type == "avatar" && fm.size() > max_avatar_size_) {
        send_system("Upload rejected: avatar size exceeds max_avatar_size.\n", send);
        return;
    }

    const std::string storage_key = FileStorage::generate_storage_key();
    const std::string tmp_rel = "tmp/" + storage_key + ".part";
    const std::string tmp_abs = storage_.tmp_path(storage_key + ".part");

    auto writer = storage_.create_writer(tmp_abs);
    if (!writer || !writer->open()) {
        send_system("Failed to create upload temporary file.\n", send);
        return;
    }

    const uint64_t total = fm.size();
    const uint32_t cs = chunk_size_;
    const uint32_t chunk_count = static_cast<uint32_t>((total + cs - 1) / cs);

    file_transfer_info t;
    t.file_id      = -1;
    t.uploader_uid = uid;
    t.receiver_uid = msg.target_uid() > 0 ? msg.target_uid() : -1;
    t.direction    = "upload";
    t.status       = "uploading";
    t.total_size   = total;
    t.transferred_size = 0;
    t.chunk_size   = cs;
    t.chunk_count  = chunk_count;
    t.next_chunk_index = 0;
    t.tmp_path     = tmp_rel;
    // 持久化上传元数据，断线重连后可按 next_chunk_index 重建 writer。
    t.storage_key   = storage_key;
    t.file_type     = type;
    t.original_name = fm.original_name();
    t.mime_type     = fm.mime_type();

    const int transfer_id = repo_->files()->create_transfer(t);
    if (transfer_id < 0) {
        writer->abort();
        send_system("Failed to create upload transfer record.\n", send);
        return;
    }
    t.transfer_id = transfer_id;

    UploadCtx ctx;
    ctx.transfer_id      = transfer_id;
    ctx.storage_key      = storage_key;
    ctx.type             = type;
    ctx.original_name    = fm.original_name();
    ctx.mime_type        = fm.mime_type();
    ctx.tmp_path         = tmp_rel;
    ctx.total_size       = total;
    ctx.chunk_size       = cs;
    ctx.chunk_count      = chunk_count;
    ctx.transferred      = 0;
    ctx.next_chunk_index = 0;
    ctx.declared_hash    = fm.hash();
    ctx.writer           = std::move(writer);
    uploads_[transfer_id] = std::move(ctx);

    chat_proto::Envelope resp;
    resp.set_type("file_upload_ready");
    fill_transfer_proto(t, resp.mutable_transfer_info());
    send(resp, {});
}

void FileService::upload_chunk(int /*uid*/, const chat_proto::Envelope& msg,
                               const std::string& file_data, const SendFn& send) {
    const auto& meta = msg.meta();
    int transfer_id = -1;
    try {
        transfer_id = std::stoi(meta.transfer_id());
    } catch (...) {
        send_system("Invalid transfer_id in file chunk.\n", send);
        return;
    }

    auto it = uploads_.find(transfer_id);
    if (it == uploads_.end()) {
        send_system("Unknown upload transfer; send file_upload_init first.\n", send);
        return;
    }

    auto& ctx = it->second;
    if (!ctx.writer || !ctx.writer->is_open()) {
        send_system("Upload writer is not available.\n", send);
        return;
    }

    const uint32_t idx = meta.chunk_index();
    if (idx >= ctx.chunk_count) {
        send_system("chunk_index out of range.\n", send);
        return;
    }
    const uint64_t offset = meta.offset();
    if (!ctx.writer->write_at(offset, file_data.data(), file_data.size())) {
        send_system("Failed to write file chunk to disk.\n", send);
        return;
    }

    mark_received(ctx.received, idx);

    uint64_t transferred = 0;
    for (uint32_t i = 0; i < ctx.chunk_count; ++i) {
        if (is_received(ctx.received, i)) {
            transferred += chunk_bytes(i, ctx.total_size, ctx.chunk_size, ctx.chunk_count);
        }
    }
    ctx.transferred = transferred;
    ctx.next_chunk_index = first_missing(ctx.received, ctx.chunk_count);

    file_transfer_info t;
    t.transfer_id      = ctx.transfer_id;
    t.status           = "uploading";
    t.transferred_size = ctx.transferred;
    t.next_chunk_index = ctx.next_chunk_index;
    t.received_bitmap  = bitmap_hex(ctx.received);
    t.tmp_path         = ctx.tmp_path;
    repo_->files()->update_transfer(t);
}

void FileService::upload_done(int uid, const chat_proto::Envelope& msg, const SendFn& send) {
    const int transfer_id = msg.transfer_id();
    auto it = uploads_.find(transfer_id);
    if (it == uploads_.end()) {
        send_system("Unknown upload transfer.\n", send);
        return;
    }

    auto& ctx = it->second;
    if (!all_received(ctx.received, ctx.chunk_count)) {
        send_system("Upload incomplete; some chunks are missing.\n", send);
        return;
    }

    const std::string final_path = storage_.resolve(ctx.type, ctx.storage_key);
    if (!ctx.writer->commit(final_path)) {
        send_system("Failed to commit uploaded file.\n", send);
        return;
    }

    // 统一从磁盘完整重算 sha256（兼容乱序/续传）。
    const std::string final_hash = hash_file(storage_, final_path);
    if (!ctx.declared_hash.empty() && ctx.declared_hash != final_hash) {
        storage_.remove_file(final_path);
        file_transfer_info failed;
        failed.transfer_id = transfer_id;
        failed.status      = "failed";
        failed.tmp_path.clear();
        repo_->files()->update_transfer(failed);
        uploads_.erase(it);
        send_system("Upload failed: sha256 mismatch.\n", send);
        return;
    }

    const int file_id = repo_->files()->create_file(
        uid, ctx.storage_key, ctx.type, ctx.original_name, ctx.mime_type, ctx.total_size, final_hash);
    if (file_id < 0) {
        storage_.remove_file(final_path);
        send_system("Failed to save file metadata.\n", send);
        return;
    }

    repo_->files()->set_transfer_file_id(transfer_id, file_id);

    file_transfer_info t;
    t.transfer_id      = transfer_id;
    t.file_id          = file_id;
    t.status           = "completed";
    t.transferred_size = ctx.total_size;
    t.next_chunk_index = ctx.chunk_count;
    t.tmp_path.clear();
    repo_->files()->update_transfer(t);

    file_meta_info f;
    repo_->files()->get_file(file_id, f);

    file_transfer_info tfull;
    repo_->files()->get_transfer(transfer_id, tfull);

    // 存储总量超限时，淘汰最老文件（磁盘 + DB）。
    evict_if_needed();

    chat_proto::Envelope resp;
    resp.set_type("file_upload_complete");
    fill_file_proto(f, resp.mutable_file_meta());
    fill_transfer_proto(tfull, resp.mutable_transfer_info());
    send(resp, {});

    uploads_.erase(it);
}

// ------------------------------------------------------------
// 下载（M4：拉取式分片流，客户端逐块请求，天然背压/可中断）
// ------------------------------------------------------------
void FileService::download_init(int uid, const chat_proto::Envelope& msg, const SendFn& send) {
    if (uid < 0) {
        send_system("You must be logged in to download files.\n", send);
        return;
    }

    int file_id = -1;
    try {
        file_id = std::stoi(msg.file_id());
    } catch (...) {
        send_system("Invalid file_id.\n", send);
        return;
    }

    file_meta_info f;
    if (!repo_->files()->get_file(file_id, f)) {
        send_system("File not found.\n", send);
        return;
    }

    const uint32_t cs = chunk_size_;
    const uint32_t chunk_count = static_cast<uint32_t>((f.size + cs - 1) / cs);

    file_transfer_info t;
    t.file_id      = f.file_id;
    t.uploader_uid = f.uploader_uid;
    t.receiver_uid = uid;
    t.direction    = "download";
    t.status       = "downloading";
    t.total_size   = f.size;
    t.transferred_size = 0;
    t.chunk_size   = cs;
    t.chunk_count  = chunk_count;
    t.next_chunk_index = 0;

    const int transfer_id = repo_->files()->create_transfer(t);
    if (transfer_id < 0) {
        send_system("Failed to create download transfer record.\n", send);
        return;
    }
    t.transfer_id = transfer_id;

    chat_proto::Envelope ready;
    ready.set_type("file_download_ready");
    fill_file_proto(f, ready.mutable_file_meta());
    fill_transfer_proto(t, ready.mutable_transfer_info());
    send(ready, {});

    // 不再自动推块：客户端收到 ready 后逐块发 file_download_chunk 拉取。
}

// 拉取单个分片：服务端只读这一块并回 file_chunk，内存/发送队列都有界（背压）。
void FileService::download_chunk(int /*uid*/, const chat_proto::Envelope& msg, const SendFn& send) {
    const int transfer_id = msg.transfer_id();
    const int chunk_index = msg.chunk_index();

    file_transfer_info t;
    if (!repo_->files()->get_transfer(transfer_id, t)) {
        send_system("Transfer not found.\n", send);
        return;
    }
    if (t.direction != "download") {
        send_system("Not a download transfer.\n", send);
        return;
    }
    if (t.status == "completed" || t.status == "cancelled" || t.status == "failed") {
        send_system("Transfer is not active.\n", send);
        return;
    }
    if (chunk_index < 0 || static_cast<uint32_t>(chunk_index) >= t.chunk_count) {
        send_system("Invalid chunk_index.\n", send);
        return;
    }

    file_meta_info f;
    if (!repo_->files()->get_file(t.file_id, f)) {
        send_system("File not found.\n", send);
        return;
    }

    auto reader = storage_.open_reader(storage_.resolve(f.type, f.storage_key));
    if (!reader || !reader->open()) {
        send_system("Failed to open file on disk.\n", send);
        return;
    }

    const uint64_t offset = static_cast<uint64_t>(chunk_index) * t.chunk_size;
    if (!reader->seek(offset)) {
        send_system("Failed to seek file.\n", send);
        return;
    }

    std::string buf(t.chunk_size, '\0');
    const std::size_t n = reader->read(buf.data(), t.chunk_size);
    if (n == 0) {
        send_system("Failed to read file chunk.\n", send);
        return;
    }

    chat_proto::Envelope env;
    env.set_type("file_chunk");
    auto* meta = env.mutable_meta();
    meta->set_type("file_chunk");
    meta->set_transfer_id(std::to_string(transfer_id));
    meta->set_file_id(std::to_string(f.file_id));
    meta->set_filename(f.original_name);
    meta->set_total_size(f.size);
    meta->set_chunk_index(static_cast<uint32_t>(chunk_index));
    meta->set_chunk_count(t.chunk_count);
    meta->set_chunk_size(static_cast<uint32_t>(n));
    meta->set_offset(offset);
    send(env, std::string(buf.data(), n));

    // 更新进度；最后一块标记完成。
    const uint64_t done_bytes = static_cast<uint64_t>(chunk_index + 1) * t.chunk_size;
    t.transferred_size = done_bytes > t.total_size ? t.total_size : done_bytes;
    t.next_chunk_index = chunk_index + 1;
    t.status = (static_cast<uint32_t>(chunk_index + 1) >= t.chunk_count) ? "completed" : "downloading";
    repo_->files()->update_transfer(t);
}

// 断线重连续传：按 transfer_id 从 DB 恢复上传上下文并重建 writer。
void FileService::upload_resume(int uid, const chat_proto::Envelope& msg, const SendFn& send) {
    if (uid < 0) {
        send_system("You must be logged in to resume upload.\n", send);
        return;
    }
    const int transfer_id = msg.transfer_id();

    file_transfer_info t;
    if (!repo_->files()->get_transfer(transfer_id, t)) {
        send_system("Upload transfer not found.\n", send);
        return;
    }
    if (t.uploader_uid != uid) {
        send_system("You can only resume your own upload.\n", send);
        return;
    }
    if (t.direction != "upload") {
        send_system("Not an upload transfer.\n", send);
        return;
    }
    if (t.status == "completed" || t.status == "cancelled" || t.status == "failed") {
        send_system("Upload transfer is not resumable.\n", send);
        return;
    }
    if (t.tmp_path.empty() || t.storage_key.empty()) {
        send_system("Upload session metadata is incomplete.\n", send);
        return;
    }

    auto writer = storage_.create_writer(storage_.root() + "/" + t.tmp_path);
    if (!writer || !writer->open_append()) {
        send_system("Failed to reopen upload file.\n", send);
        return;
    }

    UploadCtx ctx;
    ctx.transfer_id      = transfer_id;
    ctx.storage_key      = t.storage_key;
    ctx.type             = t.file_type.empty() ? "file" : t.file_type;
    ctx.original_name    = t.original_name;
    ctx.mime_type        = t.mime_type;
    ctx.tmp_path         = t.tmp_path;
    ctx.total_size       = t.total_size;
    ctx.chunk_size       = t.chunk_size;
    ctx.chunk_count      = t.chunk_count;
    ctx.transferred      = t.transferred_size;
    ctx.next_chunk_index = t.next_chunk_index;
    ctx.resumed          = true;
    ctx.received         = hex_bitmap(t.received_bitmap, t.chunk_count);
    ctx.writer           = std::move(writer);
    uploads_[transfer_id] = std::move(ctx);

    t.status = "uploading";
    repo_->files()->update_transfer(t);

    chat_proto::Envelope resp;
    resp.set_type("file_upload_ready");
    fill_transfer_proto(t, resp.mutable_transfer_info());
    send(resp, {});
}

// ------------------------------------------------------------
// 控制
// ------------------------------------------------------------
void FileService::pause(int /*uid*/, const chat_proto::Envelope& msg, const SendFn& send) {
    const int transfer_id = msg.transfer_id();
    file_transfer_info t;
    if (!repo_->files()->get_transfer(transfer_id, t)) {
        send_system("Transfer not found.\n", send);
        return;
    }
    if (t.status == "uploading" || t.status == "downloading") {
        t.status = "paused";
        repo_->files()->update_transfer(t);
    }

    chat_proto::Envelope resp;
    resp.set_type("file_transfer_paused");
    fill_transfer_proto(t, resp.mutable_transfer_info());
    send(resp, {});
}

void FileService::resume(int /*uid*/, const chat_proto::Envelope& msg, const SendFn& send) {
    const int transfer_id = msg.transfer_id();
    file_transfer_info t;
    if (!repo_->files()->get_transfer(transfer_id, t)) {
        send_system("Transfer not found.\n", send);
        return;
    }
    if (t.status == "paused") {
        t.status = (t.direction == "upload") ? "uploading" : "downloading";
        repo_->files()->update_transfer(t);
    }

    chat_proto::Envelope resp;
    resp.set_type("file_transfer_progress");
    fill_transfer_proto(t, resp.mutable_transfer_info());
    send(resp, {});
}

void FileService::cancel(int /*uid*/, const chat_proto::Envelope& msg, const SendFn& send) {
    const int transfer_id = msg.transfer_id();
    file_transfer_info t;
    if (!repo_->files()->get_transfer(transfer_id, t)) {
        send_system("Transfer not found.\n", send);
        return;
    }

    auto it = uploads_.find(transfer_id);
    if (it != uploads_.end()) {
        if (it->second.writer) it->second.writer->abort();
        uploads_.erase(it);
    }

    t.status = "cancelled";
    t.tmp_path.clear();
    repo_->files()->update_transfer(t);

    chat_proto::Envelope resp;
    resp.set_type("file_transfer_progress");
    fill_transfer_proto(t, resp.mutable_transfer_info());
    send(resp, {});
}

void FileService::status(int /*uid*/, const chat_proto::Envelope& msg, const SendFn& send) {
    const int transfer_id = msg.transfer_id();
    file_transfer_info t;
    if (!repo_->files()->get_transfer(transfer_id, t)) {
        send_system("Transfer not found.\n", send);
        return;
    }

    chat_proto::Envelope resp;
    resp.set_type("file_transfer_progress");
    fill_transfer_proto(t, resp.mutable_transfer_info());
    send(resp, {});
}

// ------------------------------------------------------------
// 管理
// ------------------------------------------------------------
void FileService::list(int uid, const chat_proto::Envelope& msg, const SendFn& send) {
    if (uid < 0) {
        send_system("You must be logged in to list files.\n", send);
        return;
    }
    std::vector<file_meta_info> files;
    repo_->files()->list_files(uid, msg.before_id(), 50, files);

    chat_proto::Envelope resp;
    resp.set_type("file_list_response");
    for (const auto& f : files) {
        fill_file_proto(f, resp.add_files());
    }
    send(resp, {});
}

void FileService::remove_file(int /*uid*/, const chat_proto::Envelope& msg, const SendFn& send) {
    int file_id = -1;
    try {
        file_id = std::stoi(msg.file_id());
    } catch (...) {
        send_system("Invalid file_id.\n", send);
        return;
    }

    file_meta_info f;
    if (!repo_->files()->get_file(file_id, f)) {
        send_system("File not found.\n", send);
        return;
    }

    storage_.remove_file(storage_.resolve(f.type, f.storage_key));
    repo_->files()->delete_file(file_id);
    send_system("File deleted.\n", send);
}

// 存储总量达到上限后，删除最老的文件与数据库记录，直到低于上限。
void FileService::evict_if_needed() {
    const uint64_t limit = max_storage_size_;
    if (limit == 0) return;

    uint64_t usage = 0;
    if (!repo_->files()->get_storage_usage(usage)) return;

    while (usage > limit) {
        std::vector<file_meta_info> oldest;
        if (!repo_->files()->get_oldest_files(10, oldest) || oldest.empty()) break;

        bool removed_any = false;
        for (const auto& f : oldest) {
            if (usage <= limit) break;
            storage_.remove_file(storage_.resolve(f.type, f.storage_key));
            if (repo_->files()->delete_file(f.file_id)) {
                usage = (usage > f.size) ? (usage - f.size) : 0;
                removed_any = true;
                std::cout << "[file] evicted oldest file " << f.file_id << std::endl;
            }
        }
        if (!removed_any) break;
    }
}
