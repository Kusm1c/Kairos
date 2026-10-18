#include "EdgeShift.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace kpsim::search {
namespace {

constexpr Tick kProgress = 8;   // a re-timing must push the death this much later to go on

} // namespace

bool holdOrdered(const Holds& h, size_t i) {
    if (h[i].press < 1 || h[i].release <= h[i].press) return false;
    if (i && h[i - 1].release >= h[i].press) return false;
    return i + 1 >= h.size() || h[i].release < h[i + 1].press;
}

Shift shiftEdge(const Plan& plan, EdgeRef e, int d, const std::function<Try(const Plan&)>& attempt,
                const std::function<bool(const Holds&, size_t)>& valid, const ShiftOptions& so) {
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
    out.death = out.reached = r.death;
    Tick death = r.death;
    Plan furthest = p;
    size_t hold = e.hold;
    bool press = e.press;
    for (int n = 0; n < so.followUps + so.chain && (!so.budget || out.retimed < so.budget); ++n) {
        const bool chained = n >= so.followUps;
        if (!press) ++hold;
        press = !press;
        if (hold >= hs.size() || (!press && hs[hold].release == macro::kNever)) break;
        Tick& at = press ? hs[hold].press : hs[hold].release;
        const Tick was = at;
        std::optional<Plan> best;
        Tick bestDeath = 0;
        // Offsets from the input's own tick: the same shift, or its own timing, give or take.
        std::vector<int> done;
        auto tryAt = [&](int s) {
            const int64_t c = (int64_t)was + s;
            if (s == 0 || c < 1 || c > (int64_t)death || (so.budget && out.retimed >= so.budget)) return false;
            if (std::find(done.begin(), done.end(), s) != done.end()) return false;
            done.push_back(s);
            at = (Tick)c;
            bool passed = false;
            if (ok(hs, hold)) {
                Try f = attempt(p);
                ++out.retimed;
                passed = f.ok;
                if (!passed && f.death > out.reached) {
                    out.reached = f.death;
                    furthest = p;
                }
                if (!passed && f.death > bestDeath) {
                    bestDeath = f.death;
                    best = p;
                }
            }
            if (!passed) at = was;
            return passed;
        };
        bool passed = false;
        if (chained) passed = tryAt(d);
        for (int s : {d, d - 1, d + 1, -1, 1})
            if (!passed && !chained) passed = tryAt(s);
        for (int k = 2; k <= so.spread && !passed && !chained && bestDeath <= death; ++k)   // no better yet: wider
            for (int s : {d - k, d + k, -k, k})
                if (!passed) passed = tryAt(s);
        if (passed) {
            out.result = Shift::Passed;
            out.plan = std::move(p);
            out.followed = true;
            return out;
        }
        // No progress: this input does not matter here (a cube's release); leave it, try the next.
        if (!best || bestDeath < death + kProgress) {
            if (chained || (int64_t)was > (int64_t)death) break;
            continue;
        }
        p = std::move(*best);
        death = bestDeath;
    }
    out.plan = std::move(furthest);
    return out;
}

} // namespace kpsim::search
