#pragma once
#include "Engine.hpp"
#include "InputRules.hpp"

namespace kpsim::search {

// Turns a plan that finishes into a simpler one that still finishes:
// fewer clicks, then every press and release moved to the middle of its window.
struct OptimizeOptions {
    Tick mergeGap = 24;    // holds closer than this are tried as one
    int maxShift = 20;     // how far an edge's window is measured
    bool center = true;
    int comfort = 3;       // an edge with this much room each way is left where it is
    int rounds = 1;        // more merge/remove rounds while clicks keep going
    InputRules rules;      // minPress / minRelease kept by every change, and enforced first
};

// What a solve aims for, as the "solve-goal" setting names it.
enum class SolveGoal { Balanced, FewestInputs, LenientWindows };

inline OptimizeOptions optimizeFor(SolveGoal goal) {
    OptimizeOptions o;
    if (goal == SolveGoal::FewestInputs) {
        o.mergeGap = 240;
        o.rounds = 4;
    } else if (goal == SolveGoal::LenientWindows) {
        o.comfort = 6;
    }
    return o;
}

struct OptimizeResult {
    Plan plan;
    size_t clicksBefore = 0;
    size_t removed = 0, merged = 0, centered = 0;
    size_t humanized = 0;    // too short presses or gaps fixed
    size_t violations = 0;   // still too short at the end
    int narrowestBefore = -1, narrowestAfter = -1;   // ticks; -1: every timing had comfortable room
    uint64_t trials = 0;
    bool finishedBefore = false;
    bool verified = false;   // the result replayed from the start and finished
};

void optimizePlan(Engine& engine, const Plan& start, const OptimizeOptions& options, OptimizeResult& out);

} // namespace kpsim::search
