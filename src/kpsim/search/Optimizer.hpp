#pragma once
#include "Engine.hpp"
#include "InputRules.hpp"

#include <string>

namespace kpsim::search {

// Turns a plan that finishes into a simpler one that still finishes:
// fewer clicks, then every press and release moved to the middle of its window.
struct OptimizeOptions {
    Tick mergeGap = 24;    // holds closer than this are tried as one
    int maxShift = 20;     // how far an edge's window is measured
    bool center = true;
    int comfort = 3;       // an edge with this much room each way is left where it is
    int rounds = 1;        // more merge/remove rounds while inputs keep going
    bool holdToEnd = false;   // also try holding the last press to the end (one input less)
    bool transfers = false;   // also drop a hold or a gap by giving its ticks to a neighbour (wave, ship)
    int retries = 6;       // changes the final replay refuses, dropped one at a time
    // Room over clicks: a removal or merge may not leave an input around it with less than a tick
    // each way, and a wave timing without that room gets a zigzag added beside it.
    bool roomy = true;
    InputRules rules;      // minPress / minRelease kept by every change, and enforced first
};

// What a solve aims for, as the "solve-goal" setting names it.
enum class SolveGoal { Balanced, FewestInputs, LenientWindows };

inline OptimizeOptions optimizeFor(SolveGoal goal) {
    OptimizeOptions o;
    if (goal == SolveGoal::FewestInputs) {
        o.mergeGap = 240;
        o.rounds = 4;
        o.holdToEnd = true;
        o.transfers = true;
        o.roomy = false;
    } else if (goal == SolveGoal::LenientWindows) {
        o.comfort = 6;
    }
    return o;
}

struct OptimizeResult {
    Plan plan;
    size_t clicksBefore = 0, inputsBefore = 0;
    size_t removed = 0, merged = 0, centered = 0;
    size_t rejected = 0;     // changes the final replay refused, dropped
    size_t humanized = 0;    // too short presses or gaps fixed
    size_t widened = 0;      // tight wave timings given room by a zigzag beside them
    std::string widenLog;    // what each tight timing tried
    size_t violations = 0;   // still too short at the end
    int narrowestBefore = -1, narrowestAfter = -1;   // ticks; -1: every timing had comfortable room
    uint64_t trials = 0;
    bool finishedBefore = false;
    bool verified = false;   // the result replayed from the start and finished
};

void optimizePlan(Engine& engine, const Plan& start, const OptimizeOptions& options, OptimizeResult& out);

} // namespace kpsim::search
