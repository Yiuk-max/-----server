#include <mutex>
#include <vector>
#include <cstddef>

// ---------- 通用的、按类型池化的内存池 ----------
template <typename T, size_t kBlockCountPerChunk, size_t kMaxChunks>
class ClassMemoryPool {
public:
    // 返回 nullptr 表示池子彻底用不了了（达到 chunk 上限且没空闲块），
    // 调用方需要自己 fallback 到 ::operator new
    static void* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!freeList_) {
            if (!expand()) {
                return nullptr;  // 保底信号
            }
        }
        void* block = freeList_;
        freeList_ = *reinterpret_cast<void**>(freeList_);
        return block;
    }

    // 返回 false 表示这块内存不是池子分配的（是 fallback 出去的），
    // 调用方需要自己 ::operator delete
    static bool deallocate(void* p) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!belongs_to_pool(p)) {
            return false;
        }
        *reinterpret_cast<void**>(p) = freeList_;
        freeList_ = p;
        return true;
    }

private:
    // 对齐到指针大小，防止 sizeof(T) 太小放不下链表指针，或者内存不对齐
    static constexpr size_t block_size() {
        size_t s = sizeof(T) > sizeof(void*) ? sizeof(T) : sizeof(void*);
        constexpr size_t align = alignof(std::max_align_t);
        return (s + align - 1) / align * align;  // 向上取整对齐
    }

    static bool expand() {
        if (chunks_.size() >= kMaxChunks) {
            return false;  // 达到上限，不能再扩了
        }
        const size_t bs = block_size();
        void* chunk = ::operator new(bs * kBlockCountPerChunk);
        char* base = static_cast<char*>(chunk);
        char* end = base + bs * kBlockCountPerChunk;

        char* p = base;
        for (size_t i = 0; i < kBlockCountPerChunk - 1; ++i) {
            *reinterpret_cast<void**>(p) = p + bs;
            p += bs;
        }
        *reinterpret_cast<void**>(p) = freeList_;  // 接到原来的空闲链表后面
        freeList_ = base;

        chunks_.push_back({base, end});
        return true;
    }

    static bool belongs_to_pool(void* p) {
        for (auto& [start, end] : chunks_) {
            if (p >= start && p < end) return true;
        }
        return false;
    }

    static inline void* freeList_ = nullptr;
    static inline std::vector<std::pair<void*, void*>> chunks_;
    static inline std::mutex mutex_;  // 你的业务是多线程池处理，必须加锁
};