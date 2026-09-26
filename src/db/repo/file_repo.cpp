#include "file_repo.h"

#include <iostream>
#include <memory>

#include "mysql_conn_pool.h"
#include "cppconn/prepared_statement.h"
#include "cppconn/resultset.h"
#include "cppconn/statement.h"
#include "cppconn/datatype.h"
#include "cppconn/exception.h"

namespace {
// 连接 RAII：取池连接，作用域结束后归还，异常安全。
class ConnGuard {
public:
    ConnGuard() : conn_(MySQL_Conn_Pool::get_instance().get_connection()) {}
    ~ConnGuard() {
        if (conn_) {
            MySQL_Conn_Pool::get_instance().return_connection(conn_);
        }
    }
    sql::Connection* get() { return conn_; }
    explicit operator bool() const { return conn_ != nullptr; }
private:
    sql::Connection* conn_;
};

file_meta_info make_file_meta(sql::ResultSet* rs) {
    file_meta_info f;
    f.file_id      = rs->getInt("id");
    f.uploader_uid = rs->getInt("uploader_uid");
    f.storage_key  = rs->getString("storage_key");
    f.type         = rs->getString("type");
    f.original_name = rs->getString("original_name");
    f.mime_type    = rs->isNull("mime_type") ? "" : rs->getString("mime_type");
    f.size         = rs->getUInt64("size");
    f.hash         = rs->getString("hash");
    f.created_at   = rs->getString("created_at");
    return f;
}

file_transfer_info make_transfer(sql::ResultSet* rs) {
    file_transfer_info t;
    t.transfer_id      = rs->getInt("id");
    t.file_id          = rs->isNull("file_id") ? -1 : rs->getInt("file_id");
    t.uploader_uid     = rs->getInt("uploader_uid");
    t.receiver_uid     = rs->isNull("receiver_uid") ? -1 : rs->getInt("receiver_uid");
    t.direction        = rs->getString("direction");
    t.status           = rs->getString("status");
    t.total_size       = rs->getUInt64("total_size");
    t.transferred_size = rs->getUInt64("transferred_size");
    t.chunk_size       = rs->getUInt("chunk_size");
    t.chunk_count      = rs->getUInt("chunk_count");
    t.next_chunk_index = rs->getUInt("next_chunk_index");
    t.received_bitmap  = rs->isNull("rb") ? "" : rs->getString("rb");
    t.tmp_path         = rs->isNull("tmp_path") ? "" : rs->getString("tmp_path");
    t.storage_key      = rs->isNull("storage_key") ? "" : rs->getString("storage_key");
    t.file_type        = rs->isNull("file_type") ? "" : rs->getString("file_type");
    t.original_name    = rs->isNull("original_name") ? "" : rs->getString("original_name");
    t.mime_type        = rs->isNull("mime_type") ? "" : rs->getString("mime_type");
    t.created_at       = rs->getString("created_at");
    t.updated_at       = rs->getString("updated_at");
    return t;
}
} // namespace

int file_repo::create_file(int uploader_uid, const std::string& storage_key,
                           const std::string& type, const std::string& original_name,
                           const std::string& mime_type, uint64_t size,
                           const std::string& hash, std::string* out_created_at) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] create_file: no DB connection." << std::endl;
        return -1;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO file (uploader_uid, storage_key, type, original_name, mime_type, size, hash) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)"));
        pstmt->setInt(1, uploader_uid);
        pstmt->setString(2, storage_key);
        pstmt->setString(3, type);
        pstmt->setString(4, original_name);
        pstmt->setString(5, mime_type);
        pstmt->setUInt64(6, size);
        pstmt->setString(7, hash);
        pstmt->executeUpdate();

        int file_id = -1;
        std::unique_ptr<sql::ResultSet> rs(
            guard.get()->createStatement()->executeQuery(
                "SELECT id, created_at FROM file WHERE id = LAST_INSERT_ID()"));
        if (rs->next()) {
            file_id = rs->getInt(1);
            if (out_created_at) *out_created_at = rs->getString(2);
        }
        return file_id > 0 ? file_id : -1;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] create_file failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return -1;
    }
}

bool file_repo::get_file(int file_id, file_meta_info& out) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] get_file: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, uploader_uid, storage_key, type, original_name, mime_type, size, hash, created_at "
                "FROM file WHERE id = ?"));
        pstmt->setInt(1, file_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) {
            return false;
        }
        out = make_file_meta(rs.get());
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] get_file failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool file_repo::delete_file(int file_id) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] delete_file: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement("DELETE FROM file WHERE id = ?"));
        pstmt->setInt(1, file_id);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] delete_file failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool file_repo::file_exists(const std::string& storage_key) {
    ConnGuard guard;
    if (!guard) {
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT COUNT(*) FROM file WHERE storage_key = ?"));
        pstmt->setString(1, storage_key);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        return rs->next() && rs->getInt(1) > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] file_exists failed: " << e.what() << std::endl;
        return true;  // 查询失败时保守保留文件
    }
}

bool file_repo::list_files(int owner_uid, int before_id, int limit,
                           std::vector<file_meta_info>& out) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] list_files: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                before_id > 0
                    ? "SELECT id, uploader_uid, storage_key, type, original_name, mime_type, size, hash, created_at "
                      "FROM file WHERE uploader_uid = ? AND id < ? ORDER BY id DESC LIMIT ?"
                    : "SELECT id, uploader_uid, storage_key, type, original_name, mime_type, size, hash, created_at "
                      "FROM file WHERE uploader_uid = ? ORDER BY id DESC LIMIT ?"));
        pstmt->setInt(1, owner_uid);
        if (before_id > 0) {
            pstmt->setInt(2, before_id);
            pstmt->setInt(3, limit);
        } else {
            pstmt->setInt(2, limit);
        }
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            out.push_back(make_file_meta(rs.get()));
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] list_files failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool file_repo::get_storage_usage(uint64_t& out) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] get_storage_usage: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::ResultSet> rs(
            guard.get()->createStatement()->executeQuery(
                "SELECT COALESCE(SUM(size), 0) FROM file"));
        if (rs->next()) {
            out = rs->getUInt64(1);
        } else {
            out = 0;
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] get_storage_usage failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool file_repo::get_oldest_files(int limit, std::vector<file_meta_info>& out) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] get_oldest_files: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, uploader_uid, storage_key, type, original_name, mime_type, size, hash, created_at "
                "FROM file ORDER BY created_at ASC, id ASC LIMIT ?"));
        pstmt->setInt(1, limit);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            out.push_back(make_file_meta(rs.get()));
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] get_oldest_files failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

int file_repo::create_transfer(const file_transfer_info& t) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] create_transfer: no DB connection." << std::endl;
        return -1;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "INSERT INTO file_transfer "
                "(file_id, uploader_uid, receiver_uid, direction, status, total_size, transferred_size, "
                " chunk_size, chunk_count, next_chunk_index, received_bitmap, tmp_path, "
                " storage_key, file_type, original_name, mime_type) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, UNHEX(?), ?, ?, ?, ?, ?)"));

        if (t.file_id >= 0) pstmt->setInt(1, t.file_id);
        else                pstmt->setNull(1, sql::DataType::BIGINT);

        pstmt->setInt(2, t.uploader_uid);

        if (t.receiver_uid >= 0) pstmt->setInt(3, t.receiver_uid);
        else                     pstmt->setNull(3, sql::DataType::BIGINT);

        pstmt->setString(4, t.direction);
        pstmt->setString(5, t.status);
        pstmt->setUInt64(6, t.total_size);
        pstmt->setUInt64(7, t.transferred_size);
        pstmt->setUInt(8, t.chunk_size);
        pstmt->setUInt(9, t.chunk_count);
        pstmt->setUInt(10, t.next_chunk_index);

        if (t.received_bitmap.empty()) pstmt->setNull(11, sql::DataType::VARBINARY);
        else                           pstmt->setString(11, t.received_bitmap);

        if (t.tmp_path.empty()) pstmt->setNull(12, sql::DataType::VARCHAR);
        else                    pstmt->setString(12, t.tmp_path);

        if (t.storage_key.empty())   pstmt->setNull(13, sql::DataType::VARCHAR);
        else                         pstmt->setString(13, t.storage_key);
        if (t.file_type.empty())     pstmt->setNull(14, sql::DataType::VARCHAR);
        else                         pstmt->setString(14, t.file_type);
        if (t.original_name.empty()) pstmt->setNull(15, sql::DataType::VARCHAR);
        else                         pstmt->setString(15, t.original_name);
        if (t.mime_type.empty())     pstmt->setNull(16, sql::DataType::VARCHAR);
        else                         pstmt->setString(16, t.mime_type);

        pstmt->executeUpdate();

        int transfer_id = -1;
        std::unique_ptr<sql::ResultSet> rs(
            guard.get()->createStatement()->executeQuery(
                "SELECT id FROM file_transfer WHERE id = LAST_INSERT_ID()"));
        if (rs->next()) {
            transfer_id = rs->getInt(1);
        }
        return transfer_id > 0 ? transfer_id : -1;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] create_transfer failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return -1;
    }
}

bool file_repo::get_transfer(int transfer_id, file_transfer_info& out) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] get_transfer: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, file_id, uploader_uid, receiver_uid, direction, status, total_size, "
                "transferred_size, chunk_size, chunk_count, next_chunk_index, "
                "HEX(received_bitmap) AS rb, tmp_path, storage_key, file_type, original_name, mime_type, "
                "created_at, updated_at "
                "FROM file_transfer WHERE id = ?"));
        pstmt->setInt(1, transfer_id);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        if (!rs->next()) {
            return false;
        }
        out = make_transfer(rs.get());
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] get_transfer failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool file_repo::update_transfer(const file_transfer_info& t) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] update_transfer: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE file_transfer SET status = ?, transferred_size = ?, next_chunk_index = ?, "
                "received_bitmap = UNHEX(?), tmp_path = ? WHERE id = ?"));
        pstmt->setString(1, t.status);
        pstmt->setUInt64(2, t.transferred_size);
        pstmt->setUInt(3, t.next_chunk_index);

        if (t.received_bitmap.empty()) pstmt->setNull(4, sql::DataType::VARBINARY);
        else                           pstmt->setString(4, t.received_bitmap);

        if (t.tmp_path.empty()) pstmt->setNull(5, sql::DataType::VARCHAR);
        else                    pstmt->setString(5, t.tmp_path);

        pstmt->setInt(6, t.transfer_id);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] update_transfer failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool file_repo::set_transfer_file_id(int transfer_id, int file_id) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] set_transfer_file_id: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "UPDATE file_transfer SET file_id = ? WHERE id = ?"));
        pstmt->setInt(1, file_id);
        pstmt->setInt(2, transfer_id);
        return pstmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] set_transfer_file_id failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}

bool file_repo::get_stale_transfers(int timeout_seconds,
                                    std::vector<file_transfer_info>& out) {
    ConnGuard guard;
    if (!guard) {
        std::cerr << "[file_repo] get_stale_transfers: no DB connection." << std::endl;
        return false;
    }
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            guard.get()->prepareStatement(
                "SELECT id, file_id, uploader_uid, receiver_uid, direction, status, total_size, "
                "transferred_size, chunk_size, chunk_count, next_chunk_index, "
                "HEX(received_bitmap) AS rb, tmp_path, storage_key, file_type, original_name, mime_type, "
                "created_at, updated_at "
                "FROM file_transfer "
                "WHERE status IN ('uploading','downloading') "
                "  AND last_active_at < (NOW() - INTERVAL ? SECOND)"));
        pstmt->setInt(1, timeout_seconds);
        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());
        while (rs->next()) {
            out.push_back(make_transfer(rs.get()));
        }
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << "[file_repo] get_stale_transfers failed: " << e.what()
                  << " (ERRNO=" << e.getErrorCode() << ")" << std::endl;
        return false;
    }
}
