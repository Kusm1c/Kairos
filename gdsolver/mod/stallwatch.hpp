#pragma once
// Watchdog thread that reports which phase the main thread stalled in.
#include "mod/solver_bridge.hpp"

namespace p1 {
namespace stallwatch {
enum Phase {
    IDLE = 0, UPDATE, FASTLOOP, GD_UPDATE, RESET, DESTROY, COMPLETE, PHASE_COUNT
};
inline const char* kNames[PHASE_COUNT] = {
    "idle", "update", "fastloop", "gd_update", "resetLevel",
    "destroyPlayer", "levelComplete"
};
inline std::atomic<int> g_phase{IDLE};
inline std::atomic<long long> g_seq{0};   // number of phase changes (liveness indicator)
inline std::atomic<bool> g_run{false};
inline std::thread g_thread;

inline void set(int p) {
    g_phase.store(p, std::memory_order_relaxed);
    g_seq.fetch_add(1, std::memory_order_relaxed);
}

// Restore the parent phase on leaving (so "where we are now" stays correct even when nested)
struct Mark {
    int prev;
    explicit Mark(int p) : prev(g_phase.load(std::memory_order_relaxed)) { set(p); }
    ~Mark() { set(prev); }
};

inline void start() {
    if (g_run.exchange(true)) return;
    g_thread = std::thread([] {
        std::ofstream f(std::string(DATA_DIR) + "/stall.txt", std::ios::app);
        long long lastSeq = -1, lastTick = -1;
        int frozen = 0;   // neither phase nor tick is moving (a genuine freeze)
        int reports = 0;
        while (g_run.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            long long seq = g_seq.load(std::memory_order_relaxed);
            long long tick = g_tick;
            bool moving = (seq != lastSeq) || (tick != lastTick);
            lastSeq = seq; lastTick = tick;
            if (!g_started || g_sessionOver) { frozen = 0; continue; }
            frozen = moving ? 0 : frozen + 2;
            if (frozen < 10 || (frozen % 10) != 0 || reports >= 40) continue;
            ++reports;
            int p = g_phase.load(std::memory_order_relaxed);
            auto* pl = PlayLayer::get();
            auto* p1 = pl ? pl->m_player1 : nullptr;
            f << "FROZEN " << frozen << "s phase=" << kNames[p % PHASE_COUNT]
              << " tick=" << tick << " frame=" << g_frame
              << " attempt=" << g_attempt
              << " x=" << (p1 ? p1->getPositionX() : -1.f)
              << " y=" << (p1 ? p1->getPositionY() : -1.f)
              << " dead=" << (p1 ? (int)p1->m_isDead : -1)
              << " locked=" << (p1 ? (int)p1->m_isLocked : -1)
              << " paused=" << (pl ? (int)pl->m_isPaused : -1)
              << " dual=" << (int)solver::g_dualSeen
              << " pcCalls=" << g_pcCalls
              << " resets=" << solver::g_resetCalls
              << " dp1=" << solver::g_deathsP1 << " dp2=" << solver::g_deathsP2
              << " practice=" << (pl ? (int)pl->m_isPracticeMode : -1)
              << " ckpts=" << ((pl && pl->m_checkpointArray)
                               ? (int)pl->m_checkpointArray->count() : -1)
              << "\n";
            f.flush();
        }
    });
    g_thread.detach();
}
} // namespace stallwatch
}  // namespace p1
