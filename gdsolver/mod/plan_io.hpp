#pragma once
// Per-session state reset, plan_in.txt input loader, inject=/stopat= extras.
#include "mod/speedgate.hpp"

namespace p1 {
namespace levelslice {
enum Phase {
    Off = 0,     // the level itself, as chosen (no slice, or not decided yet)
    Sliced,      // solving on a copy without the objects the run cannot depend on
    Verifying,   // back on the level itself, flying the plan that cleared the copy
    Whole,       // the plan did not hold there: solving on the level itself from here on
    RefFlight,   // on a fresh copy, flying the plan a wall check just flew on the level, as the
                 // copy's first attempt (cfg slicefirstref, level_slice.hpp onRefFlightEnd)
};
inline int g_phase = Off;
inline void reset();
inline bool maybeSlice(GJBaseGameLayer* l);
inline bool takeCarry(std::vector<InputCmd>& out);
inline bool takeLight(std::vector<InputCmd>& out);
inline bool onVerifyDeath(long long dt, float deathX);
inline bool maybeCheckWall();
inline bool onGiveUp(const char* reason);
inline std::string summary();
inline size_t heldBytes();   // what the slice holds allocated (cfg memcensus)
}  // namespace levelslice

struct PreludeAttempt {
    std::vector<InputCmd> in;
    long long until = -1;   // restart the level when this attempt's tick reaches it (-1: only a death ends it)
};
inline std::vector<PreludeAttempt> g_prelude;
inline std::vector<InputCmd> g_finalInputs;   // the last block, kept while a prelude swaps g_cfg.inputs
inline long long g_preludePendingAttempt = -1;   // the attempt the restart was asked for: a stale one is dropped
inline bool g_preludeResetPending = false;     // asked at an `until`; the level restarts at the next safe point
inline long long g_preludeResets = 0;

inline bool g_solveMemHeld = false;
namespace dpsolve {
inline void releaseSolveMemory(const char* where);
inline void memCensus(const char* where);   // cfg memcensus (print only), repair.hpp
}

inline void resetSessionState() {
    g_solveMemHeld = false;   // (onQuit releases first, then calls this)
    g_prelude.clear();
    g_finalInputs.clear();
    g_preludeResetPending = false;
    g_preludeResets = 0;
    g_started = false; g_sessionOver = false; g_uiSession = false;
    g_attempt = 0; g_finishedAttempts = 0;
    g_nextInput = 0; g_nextToggle = 0;
    g_frame = 0; g_tick = 0; g_traceLines = 0; g_gameFrame = 0;
    g_endzoneBurn = false;   // carried into the next session, rendering would stay stopped
    g_endzoneLockStart = {};
    g_secRenderWas = -1;     // ...and the same for a section solve's hold (secRenderHold)
    speedgate::reset();      // the level changes, so rebuild
    g_paused = false; g_stepTicks = 0; g_realtimeOverride = false;
    g_dpShowSolution = false;   // a finished solve must not make the next session start as one
    g_dpSolving = false;
    watchSpeedSet(WATCH_SPEED_1X); g_visRefresh = false;
    g_pauseAtXFired = false;   // stop once again in the next session
    g_ckpt = nullptr; g_ckptTick = -1; g_headHeld = 0;
    g_btnDown = false;
    g_practiceOn = false; g_restoreDone = false; g_restorePending = false;
    g_restoreLoopDone = false;
    solver::g_log.clear();
    orbtrace::reset();
    padtrace::reset();
    orbtrace::g_lines = 0;
    // The dead-tick counters printed at every session's end (endSession) are the session's.
    deadstat::g_total = deadstat::g_dead = deadstat::g_streak = 0;
    deadstat::g_maxStreak = deadstat::g_episodes = 0;
    touchseed::reset();
    touchfire::reset();
    portalseed::reset();
    padseed::reset();
    ringseed::reset();
    grouptrace::g_on = false;
    grouptrace::reset();
    secsolve::reset();
    g_hudIter = 0;
    g_hudFixups = 0; g_hudFixupCap = 0;
    g_hudVerifiedX = 0.f; g_hudAnchorX = 0.f;
    g_hudAnchorT = 0;
    g_hudPhase.clear();
    solver::g_levelMaxX = 0;
    solver::g_prevTickX = -1e9f;
    solver::g_totalAttempts = 0;
    solver::g_lastDeathTick = 0; solver::g_lastGroundGap = 0;
    solver::g_injThisAttempt = 0; solver::g_injAtDeath = 0;
    solver::g_restoreTickDbg = -1;
    solver::g_pois.clear(); solver::g_poisBuilt = false;
    solver::g_coins.clear();
    solver::g_coinPickupTick.clear();
    solver::g_coinGdTick.clear();
    solver::g_coinGdUnmatched = 0;
    solver::g_coinMissFired = false;
    solver::g_coinMissIdx = -1;
    solver::g_endTriggers = 0;
    solver::g_endTriggerFiredTick = -1;
    solver::g_hasRotGameplay = false;
    solver::g_itemCounts.clear();
    solver::g_coinGates.clear();
    solver::g_coinNoMiss.clear();
    solver::g_coinLogLines = 0;
    route::clear();   // cfg routeprereq: one level's prerequisites, built with its coins
    itermap::clear();
    levelslice::reset();
    takeover::resetState();
    g_resetMarkLayer = nullptr;
    g_resetMarkPc = -1;
    g_resetsSkipped = 0;
    // cfg actcensus: the summary at the session's end is this level's objects, not the run's.
    itermap::g_actCensusState = itermap::ActCensus{};
    probe::reset(); g_probeRequest = 0;
    g_blockedAch = 0; g_blockedStat = 0; g_blockedCoin = 0; g_blockedProgress = 0;
    g_progressRestores = 0;
    g_soundWhileSolving = 0;
    g_soundBlockedWhileSolving = 0;
    g_secSongSeeksSkipped = 0;
    g_silenceReasserts = 0;
    notify::g_forced = 0;
    if (g_trace.is_open()) g_trace.close();
    if (g_dump.is_open()) g_dump.close();
}

inline bool loadInputsFile(const std::string& path, std::vector<InputCmd>& out, bool keepPrelude = false) {
    std::ifstream f(path);
    if (!f) return false;
    out.clear();
    std::vector<PreludeAttempt> blocks;   // only when the file has attempt= lines
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) {
            first = false;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF
                && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);
        }
        if (line.rfind("attempt=", 0) == 0) {
            blocks.emplace_back();
            continue;
        }
        if (line.rfind("until=", 0) == 0) {
            if (!blocks.empty()) blocks.back().until = atoll(line.c_str() + 6);
            continue;
        }
        if (line.rfind("input=", 0) != 0) continue;
        int t = 0, d = 0;
        if (sscanf(line.c_str() + 6, "%d,%d", &t, &d) == 2) {
            if (d & ~1) {
                writeResult("plan: " + path + " holds buttons this version does not drive (input="
                            + std::to_string(t) + "," + std::to_string(d) + ") - not loaded");
                out.clear();
                return false;
            }
            if (blocks.empty()) out.push_back({t, (d & 1) != 0});
            else blocks.back().in.push_back({t, (d & 1) != 0});
        }
    }
    if (!blocks.empty()) {
        // the last block is the clearing attempt; the ones before it are the prelude
        out = blocks.back().in;
        blocks.pop_back();
        if (!keepPrelude) {
            g_prelude = blocks;
            g_finalInputs = out;
        }
    } else if (!keepPrelude) {
        g_prelude.clear();   // a plain plan: the session has no prelude (nothing left from the file before)
        g_finalInputs.clear();
    }
    return !out.empty();
}

inline bool writeInputsFile(const std::string& path, const std::vector<InputCmd>& plan) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    for (const InputCmd& c : plan)
        f << "input=" << c.step << "," << (c.down ? 1 : 0) << "\n";
    return true;
}

inline bool writeAttemptSequence(const std::string& path, const std::vector<PreludeAttempt>& prelude,
                                 const std::vector<InputCmd>& clearing, const std::string& lineage = "") {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    int n = 0;
    auto block = [&](const std::vector<InputCmd>& in, long long until) {
        f << "attempt=" << ++n << "\n";
        if (!lineage.empty()) f << "# lineage=" << lineage << "\n";
        if (until >= 0) f << "until=" << until << "\n";
        for (const InputCmd& c : in) f << "input=" << c.step << "," << (c.down ? 1 : 0) << "\n";
    };
    for (const PreludeAttempt& p : prelude) block(p.in, p.until);
    block(clearing, -1);
    return true;
}

inline std::string solvedWithPath(const std::string& solutionPath) { return solutionPath + ".ver"; }
inline void writeSolvedWith(const std::string& solutionPath, const std::string& version) {
    std::ofstream f(solvedWithPath(solutionPath), std::ios::trunc);
    if (f) f << version << "\n";
}
inline std::string readSolvedWith(const std::string& solutionPath) {
    std::ifstream f(solvedWithPath(solutionPath));
    std::string v;
    if (f) std::getline(f, v);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
    return v;
}

struct InjectSpec {
    long long tick = -1;
    bool hasX = false, hasY = false, hasVy = false;
    double x = 0, y = 0, vy = 0;
};
inline std::vector<InjectSpec> g_injects;
inline size_t g_injectNext = 0;
inline long long g_stopAt = -1;
inline bool g_stopFired = false;   // once per attempt (reset to false at the attempt head)

inline void loadPlanExtras(const std::string& path) {
    g_injects.clear();
    g_injectNext = 0;
    g_stopAt = -1;
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) {
            first = false;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF
                && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);
        }
        if (line.rfind("stopat=", 0) == 0) {
            g_stopAt = std::atoll(line.c_str() + 7);
            continue;
        }
        if (line.rfind("inject=", 0) != 0) continue;
        InjectSpec s;
        const char* p = line.c_str() + 7;
        s.tick = std::atoll(p);
        // The rest is a sequence of ",key=value". Any order, each optional
        size_t pos = line.find(',', 7);
        while (pos != std::string::npos) {
            size_t next = line.find(',', pos + 1);
            std::string kv = line.substr(pos + 1,
                next == std::string::npos ? std::string::npos : next - pos - 1);
            auto eq = kv.find('=');
            if (eq != std::string::npos) {
                auto key = kv.substr(0, eq);
                double val = std::atof(kv.c_str() + eq + 1);
                if (key == "x")  { s.hasX = true;  s.x = val; }
                else if (key == "y")  { s.hasY = true;  s.y = val; }
                else if (key == "vy") { s.hasVy = true; s.vy = val; }
            }
            pos = next;
        }
        if (s.tick >= 0 && (s.hasX || s.hasY || s.hasVy)) g_injects.push_back(s);
    }
    std::sort(g_injects.begin(), g_injects.end(),
              [](const InjectSpec& a, const InjectSpec& b) { return a.tick < b.tick; });
}
}  // namespace p1
