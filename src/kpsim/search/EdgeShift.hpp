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
    Plan plan;               // Passed: the plan that passed; Failed: the try that went furthest
    bool followed = false;   // ... with the next inputs re-timed
    Tick death = 0;          // Failed: when the plain shift died
    Tick reached = 0;        // Failed: when `plan` died
    int retimed = 0;         // trials spent re-timing the next inputs
};

// Hold i and its neighbours still in order.
bool holdOrdered(const Holds& h, size_t i);

// How hard the next inputs are re-timed: how many, how far (ticks), at most this many trials.
struct ShiftOptions {
    int followUps = 4;
    int spread = 2;
    int budget = 0;   // 0: no limit
    int chain = 12;   // then up to this many more at the same shift, while each helps (a late wave stays late)
};
// In flight (ship, UFO, swing) a player steers on and on.
inline constexpr ShiftOptions kSteer{6, 6, 30, 0};

// Edge moved by d; if that alone fails, the next inputs on the button re-timed as a player
// correcting it would. `valid` adds rules to the hold order (human limits).
Shift shiftEdge(const Plan& plan, EdgeRef e, int d, const std::function<Try(const Plan&)>& attempt,
                const std::function<bool(const Holds&, size_t)>& valid = {}, const ShiftOptions& so = {});

} // namespace kpsim::search
