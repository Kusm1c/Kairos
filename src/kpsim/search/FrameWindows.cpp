#include "FrameWindows.hpp"
#include "EdgeShift.hpp"
#include "Timeline.hpp"

#include <algorithm>
#include <string>

namespace kpsim::search {
namespace {

constexpr Tick kLimit = 240u * 60u * 30u;   // 30 min
constexpr Tick kFinishSlack = 2400;
constexpr Tick kLearn = 240;

} // namespace

void findWindows(Engine& engine, const Plan& plan, const WindowOptions& o, WindowReport& out) {
    out = WindowReport{};
    out.cap = o.maxShift;
    Timeline tl(engine, o.spacing);

    engine.status("base run");
    engine.progress(0.0);
    engine.reset();
    std::vector<uint64_t> nearTrace;
    out.base = tl.advance(plan, kLimit, &nearTrace);
    const bool finished = out.base.outcome == Outcome::Finished;
    if (finished) out.goal = out.base.tick + kFinishSlack;
    else if (out.base.outcome == Outcome::Died) out.goal = out.base.tick > 1 ? out.base.tick - 1 : 0;
    else out.goal = kLimit;

    // Integrity: the base plan replayed from a snapshot must end the same way.
    if (tl.size() > 0) {
        engine.status("integrity check");
        tl.seek(out.base.tick / 2);
        RunResult again = engine.run(plan, out.goal);
        if (again.outcome != out.base.outcome || again.tick != out.base.tick) tl.setEnabled(false);
    }

    // Base run states pass (exact or near); failed trials' states die.
    KnownStates known, nearKnown;
    const auto& trace = tl.trace();
    for (Tick t = 1; t <= trace.size(); ++t)
        if (trace[t - 1]) known[stateKey(t, trace[t - 1])] = macro::kNever;
    for (Tick t = 1; t <= nearTrace.size(); ++t)
        if (nearTrace[t - 1]) nearKnown[stateKey(t, nearTrace[t - 1])] = macro::kNever;
    std::vector<uint64_t> rec;
    const bool fast = !o.strict;

    auto attempt = [&](const Plan& p, bool useKnown) -> Try {
        Tick c = firstDifference(plan, p);
        if (c == macro::kNever) return {true};
        Tick last = lastDifference(plan, p);
        tl.seek(c >= 2 ? c - 2 : 0);
        Tick from = engine.tick();
        Trace tr{&rec, useKnown ? &known : nullptr, last};
        if (useKnown) tr.nearKnown = &nearKnown;
        RunResult r = engine.run(p, out.goal, &tr);
        ++out.trials;
        if (r.outcome == Outcome::Converged) {
            ++out.pruned;
            return {r.end == macro::kNever, r.end};
        }
        bool ok = finished ? r.outcome == Outcome::Finished : r.outcome == Outcome::Reached;
        if (!ok && useKnown && r.outcome == Outcome::Died) {
            Tick a = std::max<Tick>({last, from, 1});
            Tick b = std::min<Tick>({r.tick - 1, a + kLearn, (Tick)rec.size()});
            for (Tick t = a; t <= b; ++t)
                if (rec[t - 1]) known.emplace(stateKey(t, rec[t - 1]), r.tick);
        }
        return {ok, r.tick};
    };
    auto passes = [&](const Plan& p, bool useKnown) { return attempt(p, useKnown).ok; };

    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < plan.channel(ch).size(); ++i) {
            const Hold& h = plan.channel(ch)[i];
            out.edges.push_back({ch, i, true, h.press});
            if (h.release != macro::kNever) out.edges.push_back({ch, i, false, h.release});
        }
    std::stable_sort(out.edges.begin(), out.edges.end(),
                     [](const EdgeWindow& a, const EdgeWindow& b) { return a.tick < b.tick; });

    for (size_t k = 0; k < out.edges.size(); ++k) {
        EdgeWindow& w = out.edges[k];
        engine.progress(0.05 + 0.85 * (double)k / (double)out.edges.size());
        engine.status("edge " + std::to_string(k + 1) + "/" + std::to_string(out.edges.size()) + "  trials " +
                      std::to_string(out.trials) + " (" + std::to_string(out.pruned) + " cut)");
        auto tryPlan = [&](const Plan& p) { return attempt(p, fast); };
        for (int dir : {-1, +1}) {
            int shift = 0;
            bool limited = false;
            std::vector<Plan> passed;      // per shift, the plan that passed
            std::vector<bool> followed;    // ... with re-timed follow-ups
            while (true) {
                if (shift >= o.maxShift) { limited = true; break; }
                Shift r = shiftEdge(plan, {w.player, w.hold, w.press}, dir * (shift + 1), tryPlan);
                if (r.result == Shift::Blocked) limited = true;
                if (r.result != Shift::Passed) break;
                followed.push_back(r.followed);
                passed.push_back(std::move(r.plan));
                ++shift;
            }
            // The reported bound is always proven to the goal.
            while (fast && shift > 0) {
                if (passes(passed[shift - 1], false)) break;
                --shift;
                limited = false;
            }
            if (std::find(followed.begin(), followed.begin() + shift, true) != followed.begin() + shift)
                ++out.followed;
            (dir < 0 ? w.early : w.late) = shift;
            (dir < 0 ? w.earlyLimited : w.lateLimited) = limited;
        }
    }

    if (o.removals) {
        size_t n = 0, total = plan.presses();
        for (int ch = 0; ch < 2; ++ch) {
            out.removable[ch].assign(plan.channel(ch).size(), false);
            for (size_t i = 0; i < plan.channel(ch).size(); ++i) {
                engine.status("removal " + std::to_string(++n) + "/" + std::to_string(total));
                engine.progress(0.9 + 0.1 * (double)n / (double)std::max<size_t>(1, total));
                Plan p = plan;
                p.channel(ch).erase(p.channel(ch).begin() + (ptrdiff_t)i);
                bool ok = passes(p, fast);
                if (ok && fast) ok = passes(p, false);
                out.removable[ch][i] = ok;
            }
        }
    }
    out.rejectedSnapshots = tl.rejected();
    out.snapshotsTrusted = tl.enabled();
    out.complete = true;
}

} // namespace kpsim::search
