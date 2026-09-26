#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// 内置 SHA-256（无外部依赖，支持增量 update）。
// 用法：
//   Sha256 h;
//   h.update(chunk0, n0);
//   h.update(chunk1, n1);
//   std::string hex = h.final_hex();   // 64 位小写十六进制
class Sha256 {
public:
    Sha256();
    void reset();
    void update(const uint8_t* data, std::size_t len);
    void update(const char* data, std::size_t len);
    std::string final_hex();  // 完成后对象不可再 update（如需重算请 reset）

private:
    void transform(const uint8_t block[64]);

    uint32_t h_[8];
    uint8_t  buffer_[64];
    std::size_t buffer_len_ = 0;
    uint64_t total_len_ = 0;
};
