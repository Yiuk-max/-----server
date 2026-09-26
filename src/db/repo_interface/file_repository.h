#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ============================================================
// 文件仓储契约（纯接口，无 SQL）。
// 数据模型见 sql/file_system.sql：
//   file          文件元数据（只描述文件，不存二进制）
//   file_transfer 传输会话（分片/断点/暂停续传）
// ============================================================

// 文件元数据（file 表协议视图）
struct file_meta_info {
    int         file_id       = -1;
    int         uploader_uid  = -1;
    std::string storage_key;          // 磁盘文件唯一键（服务器生成）
    std::string type;                 // avatar/emoji/image/attachment/file
    std::string original_name;
    std::string mime_type;
    uint64_t    size          = 0;
    std::string hash;                 // sha256（当前代码先占位，后续补计算）
    std::string created_at;
};

// 传输会话（file_transfer 表协议视图）
struct file_transfer_info {
    int         transfer_id      = -1;
    int         file_id          = -1;   // 上传完成前为 -1
    int         uploader_uid     = -1;
    int         receiver_uid     = -1;   // 无接收方时为 -1
    std::string direction;               // upload / download
    std::string status;                  // uploading/downloading/paused/completed/cancelled/failed
    uint64_t    total_size       = 0;
    uint64_t    transferred_size = 0;
    uint32_t    chunk_size       = 0;
    uint32_t    chunk_count      = 0;
    uint32_t    next_chunk_index = 0;
    std::string received_bitmap;         // hex 字符串，存库时 UNHEX
    std::string tmp_path;
    // 上传会话元数据（断线重连续传用；下载会话为空）
    std::string storage_key;
    std::string file_type;
    std::string original_name;
    std::string mime_type;
    std::string created_at;
    std::string updated_at;
};

class I_file_repo {
public:
    virtual ~I_file_repo() = default;

    // ==================== file 元数据 ====================
    // 新建文件元数据，成功返回 file_id（LAST_INSERT_ID），失败 -1。
    virtual int  create_file(int uploader_uid, const std::string& storage_key,
                             const std::string& type, const std::string& original_name,
                             const std::string& mime_type, uint64_t size,
                             const std::string& hash,
                             std::string* out_created_at = nullptr) = 0;

    // 按 file_id 查询；不存在返回 false。
    virtual bool get_file(int file_id, file_meta_info& out) = 0;

    // 删除文件元数据（磁盘文件由 FileStorage 负责删除）。
    virtual bool delete_file(int file_id) = 0;

    // 是否存在某个 storage_key（孤儿回收时判断磁盘文件是否仍被 DB 引用）。
    virtual bool file_exists(const std::string& storage_key) = 0;

    // 分页查某用户上传的文件列表（file_id 游标，before_id<=0 取最新一页）。
    virtual bool list_files(int owner_uid, int before_id, int limit,
                            std::vector<file_meta_info>& out) = 0;

    // 当前所有文件总字节数（存储总量淘汰策略用）。
    virtual bool get_storage_usage(uint64_t& out) = 0;

    // 最老的 limit 个文件（created_at 升序，淘汰策略用）。
    virtual bool get_oldest_files(int limit, std::vector<file_meta_info>& out) = 0;

    // ==================== file_transfer 会话 ====================
    // 新建传输会话，成功返回 transfer_id，失败 -1。
    virtual int  create_transfer(const file_transfer_info& t) = 0;

    // 按 transfer_id 查询；不存在返回 false。
    virtual bool get_transfer(int transfer_id, file_transfer_info& out) = 0;

    // 更新会话状态/进度/断点/位图/tmp_path/file_id（只更新调用方填写的字段）。
    virtual bool update_transfer(const file_transfer_info& t) = 0;

    // 上传完成后回填 file_id。
    virtual bool set_transfer_file_id(int transfer_id, int file_id) = 0;

    // 查询超过 timeout_seconds 秒无活动的传输会话（watchdog 用）。
    virtual bool get_stale_transfers(int timeout_seconds,
                                     std::vector<file_transfer_info>& out) = 0;
};
