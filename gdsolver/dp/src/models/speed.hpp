#pragma once

#include <cmath>
#include <cstddef>

namespace gdapprox {
struct SpeedEntry {
    double playerSpeed;
    double dxPerTick;
    bool measured;  // false = interpolated/unknown, see below
};

inline constexpr SpeedEntry kSpeedTable[] = {
    {0.7, 1.046386, true},   // 251.13 px/s, lv18/lv20, 891-tick baseline
    {0.9, 1.298250, true},   // 311.58 px/s, lv1,    780-tick baseline
    {1.1, 1.614270, true},   // 387.42 px/s, lv18/lv20, 2527-tick baseline
    {1.3, 1.950199, true},   // 468.05 px/s, lv20,   906-tick baseline
    {1.6, 2.400000, false},
};

inline double dxPerTickForSpeed(double playerSpeed, bool* measured = nullptr) {
    const SpeedEntry* best = &kSpeedTable[0];
    double bestErr = std::fabs(playerSpeed - best->playerSpeed);
    for (const SpeedEntry& e : kSpeedTable) {
        const double err = std::fabs(playerSpeed - e.playerSpeed);
        if (err < bestErr) {
            bestErr = err;
            best = &e;
        }
    }
    if (measured != nullptr) *measured = best->measured && bestErr < 1e-6;
    return best->dxPerTick;
}

inline double accelSwitchVyForSpeed(double playerSpeed) {
    double g;
    if (playerSpeed < 0.8) g = 0.940199;            // 0.7
    else if (playerSpeed < 1.0) g = 0.958199024;    // 0.9
    else if (playerSpeed < 1.2) g = 0.957199;       // 1.1
    else g = 0.961199;                              // 1.3 and 1.6, one row in GD
    const double gf = static_cast<double>(static_cast<float>(g));
    return gf + gf;
}
}  // namespace gdapprox
