#pragma once
// trace.csv / dump.csv / result.txt writers and the window-title status line.
#include "mod/session_hotkeys.hpp"

namespace p1 {
inline CheckpointObject* g_ckpt = nullptr;
inline long long g_ckptTick = -1;
inline unsigned char g_ckptOobLatch = 0;
inline constexpr std::size_t kOobLatchOff = gdoff::kPlayerOutOfBounds;   // 0xC38 on Windows
inline double g_ckptVy = 0.0, g_ckptVyRel = 0.0;
inline int g_restoreProbe = 0;
inline bool g_stepSpine = false;
inline int g_stepDepth = -1;
inline size_t g_ckptNextInput = 0, g_ckptNextToggle = 0;
struct SnapState { long long t; double x, y, vy; };
struct EntrySnap {
    long long tick;
    CheckpointObject* cp;
    size_t nextInput, nextToggle;
    int held;
    std::vector<SnapState> head;
    std::vector<uint8_t> player;
};
inline std::vector<EntrySnap> g_snaps;
inline size_t g_nextSnap = 0;
inline bool g_snapVerified = false;
inline std::vector<SnapState>* g_snapProbe = nullptr;
inline int g_headHeld = 0;
inline bool g_practiceOn = false;
inline bool g_restoreDone = false;
inline bool g_restoreLoopDone = false;   // the restoreloop measurement runs once per session
inline bool g_restorePending = false;
constexpr int TRACE_LIMIT = 1000000;

inline long long g_tick = 0;
inline int g_gameFrame = 0;
inline long long g_rotGameplayCalls = 0;

inline std::ofstream g_trace;
inline std::ofstream g_dump;

inline int curStep() {
    return (int)g_tick;
}

inline void updateWindowTitle() {
#ifdef GEODE_IS_WINDOWS
    static std::string s_last;
    std::string t = "GDSOLVER " + workerTag() + " | ";
    if (g_started && !g_sessionOver)
        t += "lv" + std::to_string(g_cfg.levelId) + (solvingNow() ? " SOLVING" : " REPLAY");
    else
        t += "idle";
    if (t == s_last) return;
    s_last = t;
    struct Ctx { DWORD pid; const char* title; };
    Ctx ctx{ GetCurrentProcessId(), t.c_str() };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == c->pid && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr)
            SetWindowTextA(h, c->title);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
#endif
}

inline void openFiles() {
    std::error_code ec;
    std::filesystem::create_directories(DATA_DIR, ec);
    g_trace.open(std::string(DATA_DIR) + "/trace.csv", std::ios::trunc);
    g_dump.open(std::string(DATA_DIR) + "/dump.csv", std::ios::trunc);
    g_trace << "frame,attempt,tick,event,a,b,c\n";
    g_dump.precision(9);
    g_dump << "frame,attempt,tick,x,y,yvel,rot,mode,upsideDown,onGround,onGround2,"
              "dead,speed,gravityMod,platXVel,vsize,gy1,gy2,"
              "dual,p2y,p2vy,p2up,p2ground,p2dead,pmin,pmax,snapuid,snapdist,"
              "camscale,gframe,ctrlOff,camx,camy,p2ground2,p2mode,p2vsize,p2x,"
              "rotch,rotidx,rotrev,firedw,bandst,bandmode,camoffy,freemode,bandforce,p2rot,"
              "p2ctrlOff\n";
}

inline void ev(const char* name, double a = 0, double b = 0, double c = 0) {
    if (g_cfg.noTrace || !g_trace.is_open() || g_traceLines >= TRACE_LIMIT) return;
    ++g_traceLines;
    g_trace << g_frame << ',' << g_attempt << ',' << curStep() << ',' << name
            << ',' << a << ',' << b << ',' << c << '\n';
}

inline void flushAll() {
    if (g_trace.is_open()) g_trace.flush();
    if (g_dump.is_open()) g_dump.flush();
}

inline const char* modeStr(PlayerObject* p) {
    if (p->m_isShip) return "ship";
    if (p->m_isBird) return "ufo";
    if (p->m_isBall) return "ball";
    if (p->m_isDart) return "wave";
    if (p->m_isRobot) return "robot";
    if (p->m_isSpider) return "spider";
    if (p->m_isSwing) return "swing";
    return "cube";
}

// Mode number for the dedupe key (matches the ordering in leveldp)
inline int modeIdx(PlayerObject* p) {
    if (p->m_isShip) return 1;
    if (p->m_isBall) return 2;
    if (p->m_isBird) return 3;
    if (p->m_isDart) return 4;
    if (p->m_isRobot) return 5;
    if (p->m_isSpider) return 6;
    if (p->m_isSwing) return 7;
    return 0;
}

inline void writeResult(const std::string& text, bool truncate) {
    std::ofstream f(std::string(DATA_DIR) + "/result.txt",
        truncate ? std::ios::trunc : std::ios::app);
    f << text << "\n";
}
}  // namespace p1
