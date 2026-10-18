#pragma once
#include "dp/state.hpp"

namespace dp {
class ThreadPool {
public:
    explicit ThreadPool(int n) {
        for (int i = 0; i < n; ++i)
            ths_.emplace_back([this, i] { g_threadSlot = i + 1; worker(); });
    }
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
            ++gen_;
            genSpin_.store(gen_, std::memory_order_release);
        }
        cvStart_.notify_all();
        for (auto& t : ths_) t.join();
    }
    size_t size() const { return ths_.size(); }
    void parallelFor(size_t count, const std::function<void(size_t)>& fn) {
        dispatch(count, fn, kChunk);
    }
    void parallelTasks(size_t count, const std::function<void(size_t)>& fn) {
        dispatch(count, fn, 1);
    }

private:
    void dispatch(size_t count, const std::function<void(size_t)>& fn,
                  size_t chunk) {
        if (ths_.empty() || count == 0 || (chunk > 1 && count <= chunk)) {
            for (size_t i = 0; i < count; ++i) fn(i);
            return;
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_ = &fn;
            count_ = count;
            chunk_ = chunk;
            next_.store(0, std::memory_order_relaxed);
            done_ = 0;
            ++gen_;
            genSpin_.store(gen_, std::memory_order_release);
        }
        cvStart_.notify_all();
        runChunks();          // the caller is a worker too
        std::unique_lock<std::mutex> lk(m_);
        cvDone_.wait(lk, [this] { return done_ == ths_.size(); });
        fn_ = nullptr;
    }
    void runChunks() {
        for (;;) {
            size_t i = next_.fetch_add(chunk_, std::memory_order_relaxed);
            if (i >= count_) return;
            const size_t e = std::min(i + chunk_, count_);
            for (; i < e; ++i) (*fn_)(i);
        }
    }
    void worker() {
        size_t seen = 0;
        for (;;) {
            {
                const auto until = std::chrono::steady_clock::now()
                                   + std::chrono::microseconds(50);
                while (genSpin_.load(std::memory_order_acquire) == seen
                       && std::chrono::steady_clock::now() < until)
                    std::this_thread::yield();
            }
            std::unique_lock<std::mutex> lk(m_);
            cvStart_.wait(lk, [this, &seen] { return gen_ != seen; });
            seen = gen_;
            if (stop_) return;
            lk.unlock();
            runChunks();
            lk.lock();
            if (++done_ == ths_.size()) {
                lk.unlock();
                cvDone_.notify_one();
            }
        }
    }
    static constexpr size_t kChunk = 32;
    size_t chunk_ = kChunk;
    std::vector<std::thread> ths_;
    std::mutex m_;
    std::condition_variable cvStart_, cvDone_;
    const std::function<void(size_t)>* fn_ = nullptr;
    size_t count_ = 0;
    std::atomic<size_t> next_{0};
    size_t gen_ = 0;
    std::atomic<size_t> genSpin_{0};   // gen_, readable without the lock (spin only)
    size_t done_ = 0;
    bool stop_ = false;
};

inline size_t g_aliveCap = 16000;  // per-layer stride cap (keeps RAM sane)
inline std::vector<std::pair<double, size_t>> g_capMap;
inline size_t capAtX(double x) {
    size_t c = g_aliveCap;
    for (const auto& e : g_capMap)
        if (x >= e.first) c = e.second;
    return c;
}
inline bool g_memStat = false;
inline size_t g_gcNodes = 8000000;
inline size_t g_memLimitMiB = 6144;
inline double g_portalDodgeMin = 0.1;
inline double g_speedDodgeMin = 0.0;
inline double g_glitchPortal = 0.0;
inline double g_glitchWave = 0.0;
inline double g_glitchEmbed = 0.0;
inline double g_coinWinY = 0.0;
inline double g_coinWinVy = 0.0;
inline double g_coinWinLen = 0.0;
inline bool g_ladderOverCap = true;
inline bool g_capEnvelope = false;
inline double g_yBound = 700.0;
inline double g_yBoundTurned = 1e9;
}  // namespace dp
