#pragma once

#include "models/speed.hpp"

namespace gdapprox {
struct ShipParams {
    // Player-frame accelerations, per 1/240 s tick.
    double holdStrong = 0.108;
    double holdWeak = 0.086;
    double releaseStrong = -0.103;
    double releaseWeak = -0.069;

    double accelSwitchVy = 1.9163980484008789;
    double accelSwitchVyFlip = -1.9163980484008789;

    double vyMaxPlayerFrame = 8.0;
    double vyMinPlayerFrame = -6.4;

    double yScale = 0.225;

    double dxPerTick = 1.29825;

    static ShipParams normal() { return ShipParams{}; }

    static ShipParams mini() {
        ShipParams p;
        p.holdStrong = 0.127;
        p.holdWeak = 0.101;
        p.releaseStrong = -0.122;
        p.releaseWeak = -0.081;
        p.vyMaxPlayerFrame = 9.412;
        p.vyMinPlayerFrame = -7.529;
        return p;
    }

    static ShipParams forSize(bool mini_) { return mini_ ? mini() : normal(); }

    static double accelSwitchVyForSpeed(double playerSpeed) {
        return gdapprox::accelSwitchVyForSpeed(playerSpeed);
    }

    static double accelSwitchVyFlipForSpeed(double playerSpeed) {
        return -accelSwitchVyForSpeed(playerSpeed);
    }

    ShipParams withSpeed(double playerSpeed) const {
        ShipParams p = *this;
        p.dxPerTick = dxPerTickForSpeed(playerSpeed);
        p.accelSwitchVy = accelSwitchVyForSpeed(playerSpeed);
        p.accelSwitchVyFlip = accelSwitchVyFlipForSpeed(playerSpeed);
        return p;
    }
};

inline constexpr int kInputLatencyTicks = 2;
}  // namespace gdapprox
