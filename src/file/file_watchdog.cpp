#include "file_watchdog.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

#include "repository_hub.h"
#include "file_repository.h"
#include "file_storage.h"
#include "server_config.h"

FileTransferWatchdog& FileTransferWatchdog::get_instance() {
    static FileTransferWatchdog instance;
    return instance;
}

void FileTransferWatchdog::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }
    running_.store(true);
    thread_ = std::thread([this]() { loop(); });
}

void FileTransferWatchdog::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    started_.store(false);
}

void FileTransferWatchdog::loop() {
    int round = 0;
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        if (!running_.load()) {
            break;
        }
        ++round;

        try {
            auto repo = std::make_shared<RepositoryHub>();
            const int timeout_s = ServerConfig::get_instance().file_transfer_timeout_seconds();
            FileStorage storage(ServerConfig::get_instance().file_storage_root());

            // 1. 清理超时传输会话与 tmp 临时文件。
            std::vector<file_transfer_info> stale;
            if (repo->files()->get_stale_transfers(timeout_s, stale)) {
                for (auto& t : stale) {
                    if (!t.tmp_path.empty()) {
                        storage.remove_file(storage.root() + "/" + t.tmp_path);
                    }
                    t.status = "failed";
                    t.tmp_path.clear();
                    repo->files()->update_transfer(t);
                    std::cout << "[watchdog] failed stale transfer " << t.transfer_id << std::endl;
                }
            }

            // 2. 孤儿回收：每 10 轮（约 10 分钟）扫描一次 files/，删除 DB 已无引用的文件。
            if (round % 10 == 0) {
                for (const auto& path : storage.list_stored_files()) {
                    const std::size_t pos = path.find_last_of('/');
                    const std::string storage_key = (pos == std::string::npos) ? path : path.substr(pos + 1);
                    if (!repo->files()->file_exists(storage_key)) {
                        storage.remove_file(path);
                        std::cout << "[watchdog] removed orphan file " << path << std::endl;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[watchdog] error: " << e.what() << std::endl;
        }
    }
}
