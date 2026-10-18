#pragma once
#include "kpsim/search/FrameWindows.hpp"

#include <Geode/Geode.hpp>

namespace kp {
void startRhythm(PlayLayer* layer, const kpsim::macro::Plan& plan, const kpsim::search::WindowReport& windows,
                 bool demo, bool follow = false);

// Follow: a click of the player that Watch kept from the game.
void rhythmFollowClick(GJBaseGameLayer* layer, int channel, bool down);

// The chart is shown on this layer.
bool rhythmActive(GJBaseGameLayer* layer);
} // namespace kp
