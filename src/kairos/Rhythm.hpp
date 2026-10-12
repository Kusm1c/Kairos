#pragma once
#include "kpsim/search/FrameWindows.hpp"

#include <Geode/Geode.hpp>

namespace kp {

// A macro's clicks as a rhythm-game chart over a normal attempt: notes scroll to a hit line,
// each press and release shows its frame window, and real clicks are judged against it.
// `windows` may be empty (no frame windows measured): notes are then judged on +-3 ticks.
void startRhythm(PlayLayer* layer, const kpsim::macro::Plan& plan, const kpsim::search::WindowReport& windows,
                 bool demo);

// The chart is shown on this layer.
bool rhythmActive(GJBaseGameLayer* layer);

} // namespace kp
