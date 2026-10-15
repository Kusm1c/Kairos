#include "EdgeShift.hpp"

#include <optional>

namespace kpsim::search {
namespace {

constexpr int kFollowUps = 4;   // next inputs re-timed after a mistimed one (same shift +-2)
constexpr Tick kProgress = 8;   // a re-timing must push the death this much later to go on

} // namespace

bool holdOrdered(const Holds& h, size_t i) {
    if (h[i].press < 1 || h[i].release <= h[i].press) return false;
    if (i && h[i - 1].release >= h[i].press) return false;
    return i + 1 >= h.size() || h[i].release < h[i + 1].press;
}

Shift shiftEdge(const Plan& plan, EdgeRef e, int d, const std::function<Try(const Plan&)>& attempt,
                const std::function<bool(const Holds&, size_t)>& valid) {
    auto ok = [&](const Holds& h, size_t i) { return holdOrdered(h, i) && (!valid || valid(h, i)); };
    Shift out;
    Plan p = plan;
    Holds& hs = p.channel(e.ch);
    Tick& edge = e.press ? hs[e.hold].press : hs[e.hold].release;
    const int64_t t = (int64_t)edge + d;
    if (t < 1 || t >= (int64_t)macro::kNever) {
        out.result = Shift::Blocked;
        return out;
    }
    edge = (Tick)t;
    if (!ok(hs, e.hold)) {
        out.result = Shift::Blocked;
        return out;
    }
    Try r = attempt(p);
    if (r.ok) {
        out.result = Shift::Passed;
        out.plan = std::move(p);
        return out;
    }
    // The next inputs, re-timed before the death.
    out.death = r.death;
    Tick death = r.death;
    size_t hold = e.hold;
    bool press = e.press;
    for (int n = 0; n < kFollowUps; ++n) {
        if (!press) ++hold;
        press = !press;
        if (hold >= hs.size() || (!press && hs[hold].release == macro::kNever)) break;
        Tick& at = press ? hs[hold].press : hs[hold].release;
        const Tick was = at;
        std::optional<Plan> best;
        Tick bestDeath = 0;
        auto tryAt = [&](int s) {
            const int64_t c = (int64_t)was + d + s;
            if (c == (int64_t)was || c < 1 || c > (int64_t)death) return false;
            at = (Tick)c;
            bool passed = false;
            if (ok(hs, hold)) {
                Try f = attempt(p);
                ++out.retimed;
                passed = f.ok;
                if (!passed && f.death > bestDeath) {
                    bestDeath = f.death;
                    best = p;
                }
            }
            if (!passed) at = was;
            return passed;
        };
        bool passed = false;
        for (int s : {0, -1, 1})
            if (!passed) passed = tryAt(s);
        if (!passed && bestDeath <= death)   // no better yet: a wider correction
            for (int s : {-2, 2})
                if (!passed) passed = tryAt(s);
        if (passed) {
            out.result = Shift::Passed;
            out.plan = std::move(p);
            out.followed = true;
            return out;
        }
        if (!best || bestDeath < death + kProgress) break;
        p = std::move(*best);
        death = bestDeath;
    }
    return out;
}

} // namespace kpsim::search
