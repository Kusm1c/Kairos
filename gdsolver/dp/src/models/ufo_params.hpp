#pragma once

#include "models/speed.hpp"

namespace gdapprox {
struct UfoParams {
    // Player-frame gravity, per tick. Negative: UFO has no thrust, only flaps.
    double gravityWeak = -0.086;    // vy <= switch
    double gravityStrong = -0.129;  // vy >  switch

    double accelSwitchVy = 1.9163980484008789;

    double flapTargetVy = 7.0;

    double vyMaxPlayerFrame = 8.0;
    double vyMinPlayerFrame = -6.4;
    double yScale = 0.225;
    double dxPerTick = 1.29825;

    static UfoParams normal() { return UfoParams{}; }

    static UfoParams mini() {
        UfoParams p;
        p.gravityWeak = -0.101;   // = -0.086 / 0.85, rounded to 3 dp
        p.gravityStrong = -0.152; // = -0.129 / 0.85, rounded to 3 dp
        p.flapTargetVy = 6.8;
        p.vyMaxPlayerFrame = 9.412;
        p.vyMinPlayerFrame = -7.529;
        return p;
    }

    static UfoParams forSize(bool mini_) { return mini_ ? mini() : normal(); }

    double flapPostVy() const { return flapTargetVy + gravityStrong; }

    UfoParams withSpeed(double playerSpeed) const {
        UfoParams p = *this;
        p.dxPerTick = dxPerTickForSpeed(playerSpeed);
        p.accelSwitchVy = accelSwitchVyForSpeed(playerSpeed);
        return p;
    }
};
}  // namespace gdapprox
