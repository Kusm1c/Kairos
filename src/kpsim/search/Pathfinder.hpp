#pragma once
#include "Engine.hpp"
#include "InputRules.hpp"

#include <string>
#include <utility>
#include <vector>

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
    InputRules rules;         // human limits, checked around the clicks a fix changes
    const KnownStates* rejoin = nullptr;
    const Plan* rejoinBase = nullptr;
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

struct GuideOptions {
    std::vector<std::pair<float, float>> points;
    std::vector<std::vector<std::pair<float, float>>> walls;
    float wallHalf = 6.f;     // how close to a wall is touching it (units)
    float corridor = 45.f;    // how far from the line the player may be over it (units)
    int player = 0;           // the button the drawing is for
    Tick drawnAt = 0;         // a tick of the base run by the drawing
    size_t nodes = 3000;
    bool repairFirst = true;   // false: straight to every route
    // Instead of a drawing: a run played by hand, kept up to `playedUntil` and joined to the base.
    const Plan* played = nullptr;
    Tick playedUntil = 0;
    Tick longest = 120;
    Tick horizon = 900;
    InputRules rules;
};

struct GuideResult {
    Plan plan;
    bool found = false;
    bool verified = false;    // replayed from the start and finished
    Tick from = 0, to = 0;    // where it differs from the base plan
    uint64_t trials = 0;
    std::string error;
    std::string log;          // what the search saw, for the log
};

void followGuide(Engine& engine, const Plan& base, const GuideOptions& options, GuideResult& out);
} // namespace kpsim::search
