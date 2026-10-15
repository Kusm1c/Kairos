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
    int earlyDeath = -1, lateDeath = -1;   // ticks to the death past the bound (not in the file)
};

struct WindowOptions {
    int maxShift = 20;
    Tick spacing = 240;
    bool removals = true;
    bool strict = false;   // every trial to the goal (slow)
    bool center = true;    // each edge played mid-window before the next is measured
};

struct WindowReport {
    RunResult base;
    Tick goal = 0;
    std::vector<EdgeWindow> edges;    // measured so far
    size_t total = 0;                 // edges to measure
    std::vector<bool> removable[2];   // per button, per hold
    int cap = 0;                      // maxShift; 0 = unknown (older files)
    size_t followed = 0;              // sides that needed the next inputs re-timed
    Plan plan;                        // the plan measured (centred)
    size_t centered = 0;              // edges moved to the middle of their window
    bool verified = false;            // `plan` replayed from the start and finished
    uint64_t retimed = 0;             // trials re-timing next inputs
    size_t predicted = 0;             // shifts passed by the last shift's correction, one tick on
    size_t splices = 0;               // centrings that rejoined the old run
    uint64_t trials = 0;
    uint64_t pruned = 0;
    size_t rejectedSnapshots = 0;
    bool snapshotsTrusted = true;   // false: replayed from the start every time
    bool complete = false;
};

// Window: the shifts that still finish, the next inputs re-timed as a player would. Edges are
// measured in order, each then played mid-window (frame stepper). Bounds replayed to the goal.
void findWindows(Engine& engine, const Plan& plan, const WindowOptions& options, WindowReport& out);

} // namespace kpsim::search
