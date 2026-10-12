#pragma once

namespace kp {

// Scene z-orders: the level is 0, the pause menu 10, popups above 100, gdsolver's overlay 1 << 20.
constexpr int kCalmZ = 1;    // the cover hiding the level while a bot searches
constexpr int kPanelZ = 2;   // a job's progress panel, above the cover

} // namespace kp
