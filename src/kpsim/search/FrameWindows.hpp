#pragma once
#include "Engine.hpp"

#include <vector>

namespace kpsim::search {

struct EdgeWindow {
    int player = 0;             // button: 0 = player 1, 1 = player 2
    size_t hold = 0;
    bool press = true;
    Tick tick = 0;
    int early = 0;              // ticks it can move earlier
    int late = 0;               // ticks it can move later
    bool earlyLimited = false;  // stopped by a neighbour edge or maxShift, not a death
    bool lateLimited = false;
};

struct WindowOptions {
    int maxShift = 20;
    Tick spacing = 240;
    bool removals = true;
    bool strict = false;   // every trial to the goal (slow)
};

struct WindowReport {
    RunResult base;
    Tick goal = 0;
    std::vector<EdgeWindow> edges;
    std::vector<bool> removable[2];   // per button, per hold
    int cap = 0;                      // maxShift; 0 = unknown (older files)
    uint64_t trials = 0;
    uint64_t pruned = 0;
    size_t rejectedSnapshots = 0;
    bool snapshotsTrusted = true;   // false: replayed from the start every time
    bool complete = false;
};

// Reported bounds and removals are always replayed to the goal.
void findWindows(Engine& engine, const Plan& plan, const WindowOptions& options, WindowReport& out);

} // namespace kpsim::search
