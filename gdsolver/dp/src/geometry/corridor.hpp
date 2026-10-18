#pragma once

#include <limits>
#include <vector>

namespace gdapprox {
enum class Verdict {
    Safe,
    Uncertain,
    Collide,
};

const char* toString(Verdict v);

struct MarginPolicy {
    // Clearance at or above this counts as clearly safe.
    double safeAbove = 8.0;
    double collideBelow = -8.0;

    Verdict classify(double clearance) const {
        if (!(clearance == clearance)) return Verdict::Uncertain;  // NaN
        if (clearance >= safeAbove) return Verdict::Safe;
        if (clearance <= collideBelow) return Verdict::Collide;
        return Verdict::Uncertain;
    }
};

class ICorridor {
public:
    virtual ~ICorridor() = default;
    virtual double clearance(double x, double y) const = 0;
};

// Piecewise-constant corridor sampled on a uniform x grid.
class BandCorridor final : public ICorridor {
public:
    struct Band {
        double loY;
        double hiY;
    };

    BandCorridor() = default;
    BandCorridor(double x0, double dx, std::vector<Band> bands)
        : x0_(x0), dx_(dx), bands_(std::move(bands)) {}

    double clearance(double x, double y) const override {
        if (bands_.empty()) return std::numeric_limits<double>::infinity();
        const auto i = static_cast<long long>((x - x0_) / dx_);
        if (i < 0 || i >= static_cast<long long>(bands_.size())) {
            return std::numeric_limits<double>::infinity();
        }
        const Band& b = bands_[static_cast<std::size_t>(i)];
        const double below = y - b.loY;
        const double above = b.hiY - y;
        return below < above ? below : above;
    }

    bool empty() const { return bands_.empty(); }
    double x0() const { return x0_; }
    double dx() const { return dx_; }
    const std::vector<Band>& bands() const { return bands_; }

private:
    double x0_ = 0.0;
    double dx_ = 1.0;
    std::vector<Band> bands_;
};
}  // namespace gdapprox
