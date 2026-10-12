#pragma once
#include "Engine.hpp"
#include "InputRules.hpp"

namespace kpsim::search {

struct PathOptions {
    Tick spacing = 120;       // snapshot spacing
    Tick horizon = 360;       // how far past the death a fix is run
    Tick progress = 60;       // a fix that dies this far past the death clears it
    size_t lookahead = 24;    // more fixes tried after the first one that clears it
    Tick window = 60;         // first search window before a death
    Tick maxWindow = 480;
    Tick limit = 240u * 60u * 30u;
    size_t maxBacktracks = 200;
    size_t knownCap = 4'000'000;   // remembered states
    bool simplify = true;     // drop clicks that are not needed
    InputRules rules;         // human limits
};

struct PathResult {
    Plan plan;                // best plan so far (also on cancel)
    RunResult last;
    Tick furthest = 0;
    uint64_t trials = 0;
    uint64_t pruned = 0;      // trials cut short on a known state
    uint64_t tooTight = 0;    // fixes dropped for timings a human cannot hit
    size_t backtracks = 0;
    size_t rejectedSnapshots = 0;
    bool solved = false;
    bool verified = false;    // replayed from the start and finished
    bool snapshotsTrusted = true;
    bool updateSensitive = false;   // finishes only with several steps per update
};

void findPath(Engine& engine, const PathOptions& options, PathResult& out);

} // namespace kpsim::search
