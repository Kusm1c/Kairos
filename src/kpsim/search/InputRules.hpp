#pragma once
#include "kpsim/macro/Macro.hpp"

namespace kpsim::search {

// What a human can play. All zero: anything goes ("Go crazy!").
struct InputRules {
    macro::Tick minPress = 0;      // shortest hold
    macro::Tick minRelease = 0;    // shortest gap between two holds
    macro::Tick minInterval = 0;   // press to next press (click rate)
    macro::Tick margin = 0;        // a new press or release must also work this many ticks off

    bool allows(const macro::Holds& h) const {
        for (size_t i = 0; i < h.size(); ++i) {
            if (h[i].release != macro::kNever && h[i].release - h[i].press < minPress) return false;
            if (i && h[i].press - h[i - 1].release < minRelease) return false;
            if (i && h[i].press - h[i - 1].press < minInterval) return false;
        }
        return true;
    }
    bool allows(const macro::Plan& p) const { return allows(p.holds) && allows(p.holds2); }
};

} // namespace kpsim::search
