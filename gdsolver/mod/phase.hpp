#pragma once
#include <algorithm>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace p1 {
namespace phase {
constexpr long long kLogMs = 1000;

struct Cur {
    std::string name;
    std::chrono::steady_clock::time_point t0{};
    long long tick = -1;
    int iter = -1;
};
using Id = unsigned long long;
struct Entry {
    Id id = 0;
    std::thread::id tid;
    bool loose = false;   // begin()/end(): one per thread, replaced by the next begin() of that thread
    Cur c;
};
inline std::mutex g_mu;
inline std::vector<Entry> g_active;
inline Id g_nextId = 1;

inline void (*g_testGap)() = nullptr;

inline Cur make(const char* name, long long tick, int iter) {
    Cur c;
    c.name = name ? name : "";
    c.t0 = std::chrono::steady_clock::now();
    c.tick = tick;
    c.iter = iter;
    return c;
}

// The caller holds g_mu.
inline void logEnd(const Cur& c) {
    const long long ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - c.t0).count();
    if (ms >= kLogMs)
        writeResult("phase: " + c.name + " " + std::to_string(ms) + " ms (t=" + std::to_string(c.tick)
                    + ", iter " + std::to_string(c.iter) + ")");
}
inline bool removeLocked(std::vector<Entry>::iterator it) {
    logEnd(it->c);
    g_active.erase(it);
    return true;
}

inline Id begin(const char* name, long long tick = -1, int iter = -1) {
    std::lock_guard<std::mutex> lk(g_mu);
    const auto me = std::this_thread::get_id();
    for (auto it = g_active.begin(); it != g_active.end(); ++it)
        if (it->loose && it->tid == me) { removeLocked(it); break; }
    Entry e;
    e.id = g_nextId++;
    e.tid = me;
    e.loose = true;
    e.c = make(name, tick, iter);
    const Id id = e.id;
    g_active.push_back(std::move(e));
    return id;
}
inline void end() {
    std::lock_guard<std::mutex> lk(g_mu);
    const auto me = std::this_thread::get_id();
    for (auto it = g_active.begin(); it != g_active.end(); ++it)
        if (it->loose && it->tid == me) { removeLocked(it); return; }
}

inline void endId(Id id) {
    std::lock_guard<std::mutex> lk(g_mu);   // the check and the end under one hold
    for (auto it = g_active.begin(); it != g_active.end(); ++it)
        if (it->id == id) {
            if (g_testGap) g_testGap();
            removeLocked(it);
            return;
        }
}

// The phase to show and the seconds it has run: the entry that began last; false when none is active.
inline bool current(std::string& name, double& seconds) {
    std::lock_guard<std::mutex> lk(g_mu);
    const Entry* best = nullptr;
    for (const Entry& e : g_active)
        if (!best || e.c.t0 >= best->c.t0) best = &e;
    if (!best) return false;
    name = best->c.name;
    seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - best->c.t0).count();
    return true;
}

// A phase for the length of a block, on whichever thread runs it.
class Scope {
public:
    Scope(const char* name, long long tick = -1, int iter = -1) {
        std::lock_guard<std::mutex> lk(g_mu);
        Entry e;
        e.id = m_id = g_nextId++;
        e.tid = std::this_thread::get_id();
        e.c = make(name, tick, iter);
        g_active.push_back(std::move(e));
    }
    Id id() const { return m_id; }
    ~Scope() {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto it = g_active.begin(); it != g_active.end(); ++it)
            if (it->id == m_id) { removeLocked(it); return; }   // gone already when endId took it
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Id m_id = 0;
};
}  // namespace phase
}  // namespace p1
