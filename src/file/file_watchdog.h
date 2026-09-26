#pragma once
#include <atomic>
#include <thread>

// ============================================================
// FileTransferWatchdog：后台清理超时的传输会话与临时文件。
// 每 60s 扫描 file_transfer 中超过 transfer_timeout_seconds 无活动
// 的 uploading/downloading 会话，删除 tmp/<...>.part 并标记 failed。
// ============================================================
class FileTransferWatchdog {
public:
    static FileTransferWatchdog& get_instance();

    void start();  // 启动后台线程（幂等）
    void stop();   // 停止后台线程

private:
    FileTransferWatchdog() = default;
    void loop();

    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> started_{false};
};
