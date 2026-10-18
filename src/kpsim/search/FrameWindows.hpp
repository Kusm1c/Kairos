#pragma once
#include "Engine.hpp"
#include "InputRules.hpp"

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
    int movedBy = 0;   // ticks it was moved to a wider window (not in the file)
    bool flying = false;   // ship, UFO or swing: windows are "recoverable" (not in the file)
    int ground = -1;   // presses: on the ground just before (0: in the air, GD buffers it), not in the file
};

// One shift of an edge as measured, to replay it (Check).
struct EdgeTest {
    int shift = 0;        // 0: the plan as measured
    bool passed = false;
    Tick death = 0;       // failed: when the try that went furthest died
    Plan plan;            // passed: the plan that passed; failed: that try
};
struct EdgeTests {
    EdgeWindow edge;
    std::vector<EdgeTest> tests;   // 0, then earlier, then later
};

struct WindowOptions {
    int maxShift = 20;
    Tick spacing = 240;
    bool removals = true;
    bool strict = false;   // every trial to the goal (slow)
    bool center = true;    // each edge played mid-window before the next is measured
    InputRules rules;      // centring never adds a breach of these
    int keepTests = 3;     // edges this narrow keep their tries (0: none)
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
    // Frame-perfect sides re-proven: wider without the pruning / only from the level start.
    size_t proven = 0, pruneLies = 0, restoreLies = 0;
    size_t moved = 0;                 // narrow edges moved to a wider window past a deadly gap
    size_t steered = 0;               // flying sides that passed by steering the next inputs
    size_t farDeaths = 0;             // trials passed as correctable: alive, or dead, long after the change
    size_t quick = 0;                 // sides of inputs that change nothing, settled in two trials
    std::vector<EdgeTests> tests;     // the tries of the narrow edges (WindowOptions::keepTests)
    uint64_t trials = 0;
    uint64_t pruned = 0;
    size_t rejectedSnapshots = 0;
    bool snapshotsTrusted = true;   // false: replayed from the start every time
    bool complete = false;
};

void findWindows(Engine& engine, const Plan& plan, const WindowOptions& options, WindowReport& out);
} // namespace kpsim::search
