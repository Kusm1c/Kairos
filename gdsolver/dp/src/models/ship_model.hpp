#pragma once

#include "core/state.hpp"
#include "geometry/corridor.hpp"
#include "models/ship_params.hpp"

namespace gdapprox {
class ShipModel final : public IApproxModel {
public:
    ShipModel() = default;
    explicit ShipModel(ShipParams normalParams, ShipParams miniParams)
        : normal_(normalParams), mini_(miniParams) {}

    ApproxResult simulate(const ApproxState& start,
                          const std::vector<ControlSegment>& controls,
                          const ICorridor* corridor,
                          const SimulateOptions& options) const override;

    // The override would otherwise hide the base class's 2-argument convenience form.
    using IApproxModel::simulate;

    static double stepVy(double vy, bool held, const ShipParams& p,
                         bool gravityFlipped = false, bool boostLatch = false) {
        const double s = gravityFlipped ? p.accelSwitchVyFlip : p.accelSwitchVy;
        const bool fallingBugged = (vy <= s);
        double a;
        if (held && !boostLatch)
            a = fallingBugged ? p.holdStrong : p.holdWeak;
        else if (boostLatch && vy < 0.0)
            a = (held && fallingBugged) ? p.holdStrong : p.holdWeak;
        else if (!held && !fallingBugged)
            a = p.releaseStrong;
        else
            a = (held && fallingBugged) ? -p.holdWeak : p.releaseWeak;
        double next = vy + a;
        if (boostLatch) return next;
        if (next > p.vyMaxPlayerFrame) next = p.vyMaxPlayerFrame;
        if (next < p.vyMinPlayerFrame) next = p.vyMinPlayerFrame;
        return next;
    }

    const ShipParams& params(bool mini) const { return mini ? mini_ : normal_; }
    void setParams(bool mini, const ShipParams& p) { (mini ? mini_ : normal_) = p; }

private:
    ShipParams normal_ = ShipParams::normal();
    ShipParams mini_ = ShipParams::mini();
};
}  // namespace gdapprox
