#pragma once

#include "core/state.hpp"
#include "geometry/corridor.hpp"
#include "models/ufo_params.hpp"

namespace gdapprox {
class UfoModel final : public IApproxModel {
public:
    UfoModel() = default;

    ApproxResult simulate(const ApproxState& start,
                          const std::vector<ControlSegment>& controls,
                          const ICorridor* corridor,
                          const SimulateOptions& options) const override;
    using IApproxModel::simulate;

    static double stepVy(double vy, bool flap, const UfoParams& p,
                         bool gravityFlipped = false, bool boostLatch = false,
                         double slopeVel = 0.0) {
        if (flap && vy < p.flapTargetVy) {
            vy = p.flapTargetVy;
            if (slopeVel > 0.0) {
                const double cap = (double)(float)(vy * 1.399999976158142);
                vy = std::min((double)((float)slopeVel * 0.5f) + vy, cap);
            }
        }
        const double s = gravityFlipped ? -p.accelSwitchVy : p.accelSwitchVy;
        const double a = (vy <= s) ? p.gravityWeak : p.gravityStrong;
        double next = vy + a;
        if (boostLatch) return next;   // 0x38ca9f: the clamp block is jumped
        if (next > p.vyMaxPlayerFrame) next = p.vyMaxPlayerFrame;
        return next < p.vyMinPlayerFrame ? p.vyMinPlayerFrame : next;
    }

    const UfoParams& params(bool mini) const { return mini ? mini_ : normal_; }
    void setParams(bool mini, const UfoParams& p) { (mini ? mini_ : normal_) = p; }

private:
    UfoParams normal_ = UfoParams::normal();
    UfoParams mini_ = UfoParams::mini();
};
}  // namespace gdapprox
