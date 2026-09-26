#pragma once

#include "file_repository.h"

// 文件仓储 —— 真实 MySQL 实现（repo 层，唯一允许 SQL 的目录）。
class file_repo : public I_file_repo {
public:
    int  create_file(int uploader_uid, const std::string& storage_key,
                     const std::string& type, const std::string& original_name,
                     const std::string& mime_type, uint64_t size,
                     const std::string& hash,
                     std::string* out_created_at = nullptr) override;

    bool get_file(int file_id, file_meta_info& out) override;

    bool delete_file(int file_id) override;

    bool file_exists(const std::string& storage_key) override;

    bool list_files(int owner_uid, int before_id, int limit,
                    std::vector<file_meta_info>& out) override;

    bool get_storage_usage(uint64_t& out) override;

    bool get_oldest_files(int limit, std::vector<file_meta_info>& out) override;

    int  create_transfer(const file_transfer_info& t) override;

    bool get_transfer(int transfer_id, file_transfer_info& out) override;

    bool update_transfer(const file_transfer_info& t) override;

    bool set_transfer_file_id(int transfer_id, int file_id) override;

    bool get_stale_transfers(int timeout_seconds,
                             std::vector<file_transfer_info>& out) override;
};
