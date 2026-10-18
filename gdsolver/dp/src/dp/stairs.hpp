#pragma once
#include "dp/level.hpp"

namespace dp {
struct StairParams {
    double threshold, little, down, big;
};
inline float advanceX(float x, float dxF) { return (float)(x + dxF); }

inline std::ofstream* g_snapOut = nullptr;
inline StairParams stairParamsFor(double dxPerTick, double size) {
    const bool full = std::fabs(size - 1.0) < 1e-3;
    if (dxPerTick < 1.15) return {1.0, 90.0, 120.0, 60.0};            // 0.7
    if (dxPerTick < 1.45) return {1.0, full ? 120.0 : 90.0, 150.0, 90.0};   // 0.9
    if (dxPerTick < 1.78) return {2.0, full ? 150.0 : 90.0, 195.0, 120.0};  // 1.1
    if (dxPerTick < 2.17) return {2.0, full ? 180.0 : 90.0, 225.0, 135.0};  // 1.3
    return {1.0, 120.0, 150.0, 90.0};                                 // no row
}
}  // namespace dp
