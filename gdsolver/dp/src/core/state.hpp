#pragma once

#include <cstdint>
#include <vector>

#include "geometry/corridor.hpp"

namespace gdapprox {
enum class Mode : std::uint8_t {
    Ship,
    Ufo,
};

enum class Size : std::uint8_t {
    Normal,
    Mini,
};

struct ApproxState {
    double x = 0.0;
    double y = 0.0;
    double vy = 0.0;
    double playerSpeed = 0.9;

    bool gravityFlipped = false;   // PlayerObject::m_isUpsideDown
    bool mini = false;
    bool held = false;

    Mode mode = Mode::Ship;

    Size size() const { return mini ? Size::Mini : Size::Normal; }
};

struct ControlSegment {
    bool held = false;
    int ticks = 0;
};

struct PointTarget {
    double x = 0.0;
    double y = 0.0;
    double radius = 20.0;   // GD collects a coin within 20 px of the centre
};

struct ApproxResult {
    ApproxState terminal;
    bool approximateCollision = false;
    bool approximateUncertain = false;
    double minimumClearance = 0.0;
    std::vector<ApproxState> trajectory;
    std::vector<double> targetClosest;
    // Number of ticks actually simulated (may be < requested if it collided).
    int ticksSimulated = 0;

    bool reachedAllTargets(const std::vector<PointTarget>& targets) const {
        if (targetClosest.size() != targets.size()) return false;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (targetClosest[i] > targets[i].radius) return false;
        }
        return true;
    }
};

struct SimulateOptions {
    bool recordTrajectory = false;
    bool stopOnCollision = true;
    MarginPolicy margin;
    std::vector<PointTarget> targets;
};

class IApproxModel {
public:
    virtual ~IApproxModel() = default;

    virtual ApproxResult simulate(const ApproxState& start,
                                  const std::vector<ControlSegment>& controls,
                                  const ICorridor* corridor,
                                  const SimulateOptions& options) const = 0;

    ApproxResult simulate(const ApproxState& start,
                          const std::vector<ControlSegment>& controls) const {
        return simulate(start, controls, nullptr, SimulateOptions{});
    }
};

int totalTicks(const std::vector<ControlSegment>& controls);
}  // namespace gdapprox
