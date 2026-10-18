#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "dp/constants.hpp"

namespace dp {
struct SearchProgress {
    std::atomic<long long> from{0};
    std::atomic<long long> tick{0};      // the layer being processed
    std::atomic<long long> horizon{0};
    std::atomic<double> x{0.0};          // frontier leader's x
    std::atomic<size_t> alive{0};        // states in the frontier
    std::atomic<bool> running{false};

    void begin(long long f, long long h) {
        from.store(f, std::memory_order_relaxed);
        horizon.store(h, std::memory_order_relaxed);
        tick.store(f, std::memory_order_relaxed);
        x.store(0.0, std::memory_order_relaxed);
        alive.store(0, std::memory_order_relaxed);
        running.store(true, std::memory_order_release);
    }
    void layer(long long t, double leadX, size_t n) {
        tick.store(t, std::memory_order_relaxed);
        x.store(leadX, std::memory_order_relaxed);
        alive.store(n, std::memory_order_relaxed);
    }
    void end() { running.store(false, std::memory_order_release); }
};

inline SearchProgress g_progress;

inline bool isCheckpointLayer(long long d) {
    if (d == 150 || d == 300 || d == 600 || d == 1200 || d == 2400) return true;
    return d >= 4800 && d % 4800 == 0;
}

struct SearchCheckpoints {
    struct Point {
        long long t0 = 0;      // the anchor the search runs from (--start's tick)
        long long tick = 0;    // the checkpoint layer: the last tick the lineage carries
        // `input=press,level`, through the plan writer's own latency conversion (planEdges)
        std::vector<std::pair<long long, int>> edges;
    };
    std::atomic<bool> enabled{false};   // a subscriber exists
    std::atomic<bool> cancel{false};
    std::atomic<uint64_t> call{0};
    // How many of this call's checkpoints the caller has judged as surviving. Reset with the call.
    std::atomic<size_t> judged{0};
    std::mutex m;
    std::vector<Point> points;          // this call's checkpoints, in publication order
    uint64_t seq = 0;                   // never restarts, so two calls never share an id

    void reset() {
        std::lock_guard<std::mutex> g(m);
        points.clear();
        ++seq;
        call.store(seq, std::memory_order_release);
        judged.store(0, std::memory_order_release);
    }
    void publish(Point p) {
        std::lock_guard<std::mutex> g(m);
        points.push_back(std::move(p));
    }
    size_t published() {
        std::lock_guard<std::mutex> g(m);
        return points.size();
    }
    bool pass(uint64_t c, size_t index) {
        std::lock_guard<std::mutex> g(m);
        if (call.load(std::memory_order_acquire) != c) return false;
        if (index != judged.load(std::memory_order_acquire) || index >= points.size()) return false;
        judged.store(index + 1, std::memory_order_release);
        return true;
    }
};

inline SearchCheckpoints g_check;
// --phaseprof (cli.hpp): per-phase wall time of the layer loop. Print only.
inline bool g_phaseProf = false;

enum Verdict {
    VerdictFailed = 0,
    VerdictPartial = 1,
    VerdictSolved = 2,
    VerdictCancelled = 3
};

struct SearchOutcome {
    int verdict = VerdictFailed;
    bool horizonCut = false;
    long long deepT = -1;    // where the frontier died (PARTIAL / FAILED); -1 = never reported
    double deepX = -1.0;
    int coinWall = -1;
    long long capHits = -1;  // -1 = no capstat line, i.e. the layer loop never ran
    long long workStates = 0;
    long long cancelT = -1;
    long long resimDead = -1;
    long long resimFirst = -1;   // first such tick, -1 = none
    const char* resimWhy = nullptr;  // cause at that tick; a string literal
    int resimUid = -1;               // the LEVEL's uid, not this build's ordinal
    float resimObjX = 0.f, resimObjY = 0.f;
    TouchMask resimTrig{};         // the walk's trigger mask at that tick
    int resimFrame = -1;             // the frame resimObjX/Y are expressed in
    long long replayDiedT = -1;
    // --rejoinuse: the tick the search joined the old plan at, -1 = it did not.
    long long rejoinT = -1;
    long long rejoinBadT = -1;
    const char* rejoinBadWhy = nullptr;   // string literal
    TouchMask needTrigMask{};    // bit b = box b was required
    TouchMask needTrigPassed{};  // bit b = the anchor starts past box b
    std::string seedRotQ;
    std::string rotQOrder;
    std::string coinGates;
    std::string coinNoPrune;
    int startRotHit = -1, startRotGiven = -1;
    std::string startRotMiss;
    int trigWinTouch = -1;
    long long trigTotal = 0, trigRelevantN = 0, trigKept = 0,
              trigDroppedRelevant = 0;
    long long trigDroppedBehind = 0, trigDroppedAhead = 0;
    double trigMaxKeptX = 0.0;
    unsigned long long trigMapSig = 0;
    std::string unsupported;
    std::string ladder;
    bool planWritten = false;

    void reset() {
        verdict = VerdictFailed;
        horizonCut = false;
        deepT = -1; deepX = -1.0; capHits = -1; replayDiedT = -1; cancelT = -1; rejoinT = -1;
        coinWall = -1;
        workStates = 0;
        rejoinBadT = -1; rejoinBadWhy = nullptr;
        resimDead = -1; resimFirst = -1; resimWhy = nullptr;
        resimUid = -1; resimObjX = 0.f; resimObjY = 0.f; resimTrig = TouchMask{};
        resimFrame = -1;
        needTrigMask = TouchMask{}; needTrigPassed = TouchMask{};
        seedRotQ.clear();
        rotQOrder.clear();
        coinGates.clear();
        coinNoPrune.clear();
        startRotHit = -1; startRotGiven = -1;
        startRotMiss.clear();
        trigWinTouch = -1;
        trigTotal = trigRelevantN = trigKept = trigDroppedRelevant = 0;
        trigDroppedBehind = trigDroppedAhead = 0;
        trigMaxKeptX = 0.0;
        trigMapSig = 0;
        unsupported.clear();
        ladder.clear();
        planWritten = false;
    }
};

inline SearchOutcome g_outcome;
}  // namespace dp
