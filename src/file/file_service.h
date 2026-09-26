#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "message.pb.h"
#include "file_storage.h"

class RepositoryHub;
struct file_meta_info;
struct file_transfer_info;

// ============================================================
// FileService：文件业务编排（上传/下载/暂停/续传/取消/进度/列表/删除）。
// 与网络层无关：结果通过 send(Envelope, file_data) 回传，由 handler 注入。
// 每个 client_session 持有一个实例，因此本类无需加锁（会话消息串行处理）。
// ============================================================
class FileService {
public:
    using SendFn = std::function<void(const chat_proto::Envelope&, const std::string&)>;

    explicit FileService(std::shared_ptr<RepositoryHub> repo);

    void upload_init(int uid, const chat_proto::Envelope& msg, const SendFn& send);
    void upload_chunk(int uid, const chat_proto::Envelope& msg, const std::string& file_data, const SendFn& send);
    void upload_done(int uid, const chat_proto::Envelope& msg, const SendFn& send);

    void download_init(int uid, const chat_proto::Envelope& msg, const SendFn& send);
    void download_chunk(int uid, const chat_proto::Envelope& msg, const SendFn& send);  // 拉取单个分片（背压）
    void upload_resume(int uid, const chat_proto::Envelope& msg, const SendFn& send);   // 断线重连续传

    void pause(int uid, const chat_proto::Envelope& msg, const SendFn& send);
    void resume(int uid, const chat_proto::Envelope& msg, const SendFn& send);
    void cancel(int uid, const chat_proto::Envelope& msg, const SendFn& send);
    void status(int uid, const chat_proto::Envelope& msg, const SendFn& send);

    void list(int uid, const chat_proto::Envelope& msg, const SendFn& send);
    void remove_file(int uid, const chat_proto::Envelope& msg, const SendFn& send);

private:
    void evict_if_needed();  // 存储超限时淘汰最老文件（磁盘 + DB）

    void send_system(const std::string& text, const SendFn& send);
    void fill_file_proto(const struct file_meta_info& f, chat_proto::FileMeta* out);
    void fill_transfer_proto(const struct file_transfer_info& t, chat_proto::FileTransferInfo* out);

    struct UploadCtx {
        int         transfer_id = -1;
        std::string storage_key;
        std::string type;
        std::string original_name;
        std::string mime_type;
        std::string tmp_path;
        uint64_t    total_size = 0;
        uint32_t    chunk_size = 0;
        uint32_t    chunk_count = 0;
        uint64_t    transferred = 0;
        uint32_t    next_chunk_index = 0;
        std::string declared_hash;   // 客户端声明的 sha256（可为空）
        bool        resumed = false; // 是否断线重连续传
        std::vector<uint8_t> received;  // 分片接收位图（1 bit / chunk）
        std::unique_ptr<FileStorage::Writer> writer;
    };

    std::shared_ptr<RepositoryHub> repo_;
    FileStorage storage_;
    uint32_t    chunk_size_;
    uint64_t    max_file_size_;
    uint64_t    max_avatar_size_;
    uint64_t    max_storage_size_;
    std::unordered_map<int, UploadCtx> uploads_;
};
