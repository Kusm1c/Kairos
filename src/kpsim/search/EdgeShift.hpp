#pragma once
#include "Engine.hpp"

#include <functional>

namespace kpsim::search {

struct Try {
    bool ok = false;
    Tick death = 0;   // failed: when it died, or the goal it missed
};

struct EdgeRef {
    int ch = 0;
    size_t hold = 0;
    bool press = true;
};

struct Shift {
    enum Result { Passed, Failed, Blocked } result = Failed;   // Blocked: out of order or against `valid`
    Plan plan;               // Passed: the plan that passed
    bool followed = false;   // ... with the next inputs re-timed
};

// Edge moved by d; if that alone fails, the next inputs on the button re-timed as a player
// correcting it would. `valid` adds rules to the hold order (human limits).
Shift shiftEdge(const Plan& plan, EdgeRef e, int d, const std::function<Try(const Plan&)>& attempt,
                const std::function<bool(const Holds&, size_t)>& valid = {});

} // namespace kpsim::search
