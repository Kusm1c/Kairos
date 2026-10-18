#pragma once
#include "mod/level_slice.hpp"
#include <ctime>

namespace p1 {
namespace takeover {
constexpr long long kLeadTicks = 2 * 240;
constexpr int kApproachMax = 250;
constexpr float kLeadPx = 1200.f;
constexpr int kApproachByX = 16;

// Every line goes to result.txt, and once the folder exists to summary.txt as well.
inline void line(const std::string& s) {
    if (!g_dir.empty()) {
        std::ofstream f(g_dir + "/summary.txt", std::ios::app);
        if (f) f << s << "\n";
    }
    writeResult("takeover: " + s);
}

inline int presses(const std::vector<InputCmd>& v) {
    int n = 0;
    for (const InputCmd& c : v) n += c.down ? 1 : 0;
    return n;
}

inline std::string attemptName(int n) {
    char b[32];
    snprintf(b, sizeof(b), "att_%03d.plan.txt", n);
    return b;
}

inline const Cp* cpByN(int n) {
    for (const Cp& c : g_cps)
        if (c.n == n) return &c;
    return nullptr;
}

inline std::vector<InputCmd> attemptPlan() {
    std::vector<InputCmd> plan;
    if (const Cp* c = cpByN(g_from)) plan = c->before;
    plan.insert(plan.end(), g_human.begin(), g_human.end());
    return plan;
}

inline void startWait() {
    g_waiting = true;
    g_paused = true;
    g_waitUntil = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds((long long)(std::max(0.0, g_cfg.takeoverWait) * 1000.0));
}

// How many update calls this frame flies towards the point (0: the person's speed, or no takeover).
inline int approachLoops(long long tick, float x) {
    if (!approaching()) return 0;
    if (g_cfg.takeoverAt >= 0) {
        const long long rem = g_cfg.takeoverAt - kLeadTicks - tick;
        if (rem < 8) return 0;
        return (int)std::min<long long>(rem / 4, kApproachMax);
    }
    return (x < g_cfg.takeoverX - kLeadPx) ? kApproachByX : 0;
}

inline std::string cpLine(const char* what, const Cp& c) {
    char b[256];
    snprintf(b, sizeof(b), "%s n=%d t=%lld x=%.4f y=%.4f vy=%.4f held=%d dump=%d parent=%d source=%s",
             what, c.n, c.tick, (double)c.x, (double)c.y, (double)c.vy,
             (c.rec && c.rec->held) ? 1 : 0, c.dumpAttempt, c.parent, c.source);
    return b;
}

// The start point's checkpoint is in (takeoverTake): the person has the input from its tick.
inline void begin(Cp&& c) {
    char stamp[32];
    {
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
    }
    g_dir = std::string(DATA_DIR) + "/takeover/" + stamp;
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    if (ec) writeResult("takeover: could not create " + g_dir + " (" + ec.message() + ")");
    c.n = g_nextN = 1;
    ++g_nextN;
    g_T = c.tick;
    writeInputsFile(g_dir + "/base.plan.txt", c.before);
    g_live = true;
    g_from = c.n;
    g_attemptNo = 1;
    g_human.clear();
    g_written = false;
    char b[320];
    snprintf(b, sizeof(b),
             "start level=%d t=%lld x=%.4f y=%.4f vy=%.4f held=%d base=%zu wait=%.2fs dump=%d "
             "end-key=%s - the input is the person's from t=%lld",
             g_cfg.levelId, c.tick, (double)c.x, (double)c.y, (double)c.vy,
             (c.rec && c.rec->held) ? 1 : 0, c.before.size(), g_cfg.takeoverWait, g_attempt,
             keyName(Key::TakeoverEnd).c_str(), c.tick);
    line(b);
    line("folder " + g_dir);
    line(cpLine("checkpoint", c));
    g_cps.push_back(std::move(c));
    startWait();
}

// A strict checkpoint in place of one the game placed.
inline void addCp(Cp&& c) {
    c.n = g_nextN++;
    line(cpLine("checkpoint", c));
    g_cps.push_back(std::move(c));
}

inline void dropPending() {
    g_pending = Cp{};
    g_hasPending = g_commitPending = false;
    g_pendingSrc = nullptr;
}

inline void commitPending() {
    g_commitPending = false;
    if (!g_hasPending) return;
    Cp c = std::move(g_pending);
    dropPending();
    addCp(std::move(c));
}

// The newest goes, except the start point's.
inline void removeLast(const char* why) {
    if (g_cps.size() <= 1) {
        line(std::string("remove refused (") + why + "): the start point's checkpoint stays");
        return;
    }
    line(cpLine((std::string("removed (") + why + ")").c_str(), g_cps.back()));
    g_cps.pop_back();
}

inline bool g_xRemoving = false;   // the game's removeCheckpoint below is the X key's

inline void gameRemoved(const void* gone) {
    if (!personDriving() || g_restoring || !gone) return;
    if (g_cpWanted && gone == g_wantedSrc) {
        g_cpWanted = false;
        g_wantedSrc = nullptr;
        line("the game took back the checkpoint it had just placed - no strict one for it");
        return;
    }
    if (g_cps.size() > 1 && g_cps.back().gdSrc == gone)
        removeLast(g_xRemoving ? "X" : "the game took its own back");
}

inline void removeByKey(PlayLayer* pl) {
    if (g_cps.size() <= 1) {
        removeLast("X");
        return;
    }
    const void* src = g_cps.back().gdSrc;
    if (pl && src && pl->m_checkpointArray && pl->m_checkpointArray->count() > 0
        && pl->m_checkpointArray->lastObject() == src) {
        const size_t before = g_cps.size();
        g_xRemoving = true;
        pl->removeCheckpoint(false);
        g_xRemoving = false;
        if (g_cps.size() < before) return;
    }
    removeLast("X");
}

// The attempt in progress, written out whole.
inline void writeAttempt(const char* outcome, long long tick, float x) {
    if (g_written) return;
    g_written = true;
    const std::string name = attemptName(g_attemptNo);
    if (!writeInputsFile(g_dir + "/" + name, attemptPlan()))
        writeResult("takeover: could not write " + g_dir + "/" + name);
    const Cp* c = cpByN(g_from);
    char b[288];
    snprintf(b, sizeof(b),
             "attempt=%d outcome=%s tick=%lld x=%.3f cp=%d from=%lld presses=%d other=%d dump=%d file=%s",
             g_attemptNo, outcome, tick, (double)x, g_from, c ? c->tick : -1LL, presses(g_human),
             g_other, g_attempt, name.c_str());
    line(b);
}

// The restore landed (takeoverRestore): where, against where the checkpoint was taken.
inline void restored(const Cp& c, float x, float y, float vy) {
    ++g_restores;
    ++g_attemptNo;
    g_from = c.n;
    g_human.clear();
    g_other = 0;
    g_written = false;
    const bool exact = x == c.x && y == c.y && vy == c.vy;
    if (!exact) ++g_missed;
    char b[256];
    snprintf(b, sizeof(b), "restore attempt=%d cp=%d t=%lld dump=%d %s", g_attemptNo, c.n, c.tick,
             g_attempt, exact ? "landed where the checkpoint was taken" : "");
    std::string s = b;
    if (!exact) {
        char m[200];
        snprintf(m, sizeof(m), "MISSED: x=%.4f y=%.4f vy=%.4f, taken at x=%.4f y=%.4f vy=%.4f",
                 (double)x, (double)y, (double)vy, (double)c.x, (double)c.y, (double)c.vy);
        s += m;
    }
    line(s);
    startWait();
}

inline void noteInput(bool down, int button, bool isPlayer1) {
    bool twoPlayer = false;
    if (auto* pl = PlayLayer::get())
        if (pl->m_levelSettings) twoPlayer = pl->m_levelSettings->m_twoPlayerMode;
    if (button == 1 && (isPlayer1 || !twoPlayer)) {
        g_human.push_back({(int)g_tick, down});
        return;
    }
    if (++g_other <= 3) {
        char b[160];
        snprintf(b, sizeof(b), "a press the plan format cannot carry: button=%d player1=%d down=%d t=%lld",
                 button, isPlayer1 ? 1 : 0, down ? 1 : 0, (long long)g_tick);
        line(b);
    }
}

inline void finish(const std::string& why) {
    g_live = false;
    g_done = true;
    g_waiting = false;
    g_endWhy = why;
    char b[200];
    snprintf(b, sizeof(b), "end %s attempts=%d deaths=%d restores=%d missed=%d checkpoints=%zu",
             why.c_str(), g_attemptNo, g_deaths, g_restores, g_missed, g_cps.size());
    line(b);
}

inline void died(long long tick, float x) {
    ++g_deaths;
    writeAttempt("death", tick, x);
}

inline void diedBeforePoint(long long tick, float x) {
    g_done = true;
    g_endWhy = "the plan died before the point";
    char b[200];
    if (g_cfg.takeoverAt >= 0)
        snprintf(b, sizeof(b), "takeover: the plan died at t=%lld x=%.1f, before the point t=%lld - "
                 "nothing was handed over", tick, (double)x, g_cfg.takeoverAt);
    else
        snprintf(b, sizeof(b), "takeover: the plan died at t=%lld x=%.1f, before the point x=%.1f - "
                 "nothing was handed over", tick, (double)x, (double)g_cfg.takeoverX);
    writeResult(b);
    endSession("takeover_plan_died");
}

inline void cleared(long long tick, float x) {
    writeAttempt("clear", tick, x);
    finish("clear");
}

// The end key. Outside a takeover session the key is not polled.
inline void endByKey() {
    if (g_done || !g_started || g_sessionOver) return;
    if (g_live) {
        auto* pl = PlayLayer::get();
        const float x = (pl && pl->m_player1) ? pl->m_player1->getPositionX() : -1.f;
        writeAttempt("ended", g_tick, x);
        finish("end key");
    } else {
        g_done = true;
        g_endWhy = "end key before the point";
        writeResult("takeover: end key before the point was reached - nothing was handed over");
    }
    g_paused = false;
    endSession("takeover_end_key");
}

// The HUD's takeover line (hud.hpp), "" when there is nothing to say.
inline std::string hudText() {
    if (!armed() || !g_started) return "";
    char b[384];
    if (g_live) {
        char wait[48] = "";
        if (g_waiting) {
            const double left = std::chrono::duration<double>(
                g_waitUntil - std::chrono::steady_clock::now()).count();
            snprintf(wait, sizeof(wait), "   starts in %.1fs", left > 0.0 ? left : 0.0);
        }
        char miss[48] = "";
        if (g_missed) snprintf(miss, sizeof(miss), "\nRESTORE MISSED %d time(s) - see summary", g_missed);
        const long long at = g_cps.empty() ? g_T : g_cps.back().tick;
        snprintf(b, sizeof(b),
                 "HUMAN INPUT from t=%lld   attempt %d   deaths %d%s\n"
                 "checkpoints %zu (strict; back to t=%lld)   Z place   X remove (the start stays)\n"
                 "%s   end the takeover   -   nothing is recorded%s",
                 g_T, g_attemptNo, g_deaths, wait, g_cps.size(), at,
                 keyName(Key::TakeoverEnd).c_str(), miss);
    } else if (g_done) {
        snprintf(b, sizeof(b), "TAKEOVER over (%s)", g_endWhy.c_str());
    } else if (g_cfg.takeoverAt >= 0) {
        snprintf(b, sizeof(b), "TAKEOVER: flying the plan to t=%lld (now t=%lld)",
                 g_cfg.takeoverAt, (long long)g_tick);
    } else {
        snprintf(b, sizeof(b), "TAKEOVER: flying the plan to x=%.0f", (double)g_cfg.takeoverX);
    }
    return b;
}
}  // namespace takeover
}  // namespace p1
