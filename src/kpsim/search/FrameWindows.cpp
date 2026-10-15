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
    Plan cur = plan;   // centred as edges are measured

    auto attempt = [&](const Plan& p, bool useKnown) -> Try {
        Tick c = firstDifference(cur, p);
        if (c == macro::kNever) return {true};
        Tick last = lastDifference(cur, p);
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

    // p becomes the measured plan. Run until it rejoins the old run (spliced in), else to the end.
    auto rebase = [&](const Plan& p) {
        const Tick c = firstDifference(cur, p);
        tl.seek(c >= 2 ? c - 2 : 0);
        const Tick from = engine.tick();
        std::vector<uint64_t> exact, nearRec;
        Trace tr{&exact, &known, lastDifference(cur, p)};
        tr.nearRecord = &nearRec;
        tr.nearKnown = &nearKnown;
        RunResult r = engine.run(p, out.goal, &tr);
        ++out.trials;
        if (r.outcome == Outcome::Converged && r.end == macro::kNever) {
            tl.splice(p, from, r.tick, exact);
            if (nearTrace.size() < r.tick) nearTrace.resize(r.tick, 0);
            for (Tick t = std::max<Tick>(from, 1); t < r.tick; ++t) {
                if (t <= exact.size() && exact[t - 1]) known[stateKey(t, exact[t - 1])] = macro::kNever;
                if (t <= nearRec.size() && nearRec[t - 1]) {
                    nearTrace[t - 1] = nearRec[t - 1];
                    nearKnown[stateKey(t, nearRec[t - 1])] = macro::kNever;
                }
            }
            ++out.splices;
        } else if (r.outcome == Outcome::Finished) {
            tl.truncate(from);
            tl.seek(from);
            if (nearTrace.size() > engine.tick()) nearTrace.resize(engine.tick());
            if (tl.advance(p, kLimit, &nearTrace).outcome != Outcome::Finished) return false;
            known.clear();
            nearKnown.clear();
            for (Tick t = 1; t <= trace.size(); ++t)
                if (trace[t - 1]) known[stateKey(t, trace[t - 1])] = macro::kNever;
            for (Tick t = 1; t <= nearTrace.size(); ++t)
                if (nearTrace[t - 1]) nearKnown[stateKey(t, nearTrace[t - 1])] = macro::kNever;
        } else {
            return false;
        }
        cur = p;
        // Learnt deaths assumed the old inputs.
        for (auto it = known.begin(); it != known.end();)
            it = it->second != macro::kNever ? known.erase(it) : std::next(it);
        return true;
    };

    // Edges join the report once measured: a stopped run shows only those.
    std::vector<EdgeWindow> edges;
    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < plan.channel(ch).size(); ++i) {
            const Hold& h = plan.channel(ch)[i];
            if (finished && h.press > out.base.tick) continue;   // after the end
            edges.push_back({ch, i, true, h.press});
            if (h.release != macro::kNever && (!finished || h.release <= out.base.tick))
                edges.push_back({ch, i, false, h.release});
        }
    std::stable_sort(edges.begin(), edges.end(),
                     [](const EdgeWindow& a, const EdgeWindow& b) { return a.tick < b.tick; });
    out.total = edges.size();

    for (size_t k = 0; k < edges.size(); ++k) {
        EdgeWindow& w = edges[k];
        engine.progress(0.05 + 0.85 * (double)k / (double)edges.size());
        engine.status("edge " + std::to_string(k + 1) + "/" + std::to_string(edges.size()) + "  trials " +
                      std::to_string(out.trials) + " (" + std::to_string(out.pruned) + " cut)  centred " +
                      std::to_string(out.centered));
        const Hold& now = cur.channel(w.player)[w.hold];
        w.tick = w.press ? now.press : now.release;
        auto tryPlan = [&](const Plan& p) { return attempt(p, fast); };
        std::vector<Plan> bounds[2];   // earlier, later: the plan that passed at each shift
        for (int dir : {-1, +1}) {
            int shift = 0;
            bool limited = false;
            std::vector<Plan>& passed = bounds[dir > 0];
            std::vector<bool> followed;    // ... with re-timed follow-ups
            int death = -1;
            while (true) {
                if (shift >= o.maxShift) { limited = true; break; }
                Shift r;
                // The correction that passed one tick closer, one tick further.
                if (!passed.empty() && followed.back()) {
                    Plan q = passed.back();
                    Holds& qh = q.channel(w.player);
                    const Holds& ch = cur.channel(w.player);
                    bool fine = true;
                    for (size_t i = 0; i < qh.size() && fine; ++i) {
                        for (Tick* e : {&qh[i].press, &qh[i].release}) {
                            const Tick was = e == &qh[i].press ? ch[i].press : ch[i].release;
                            if (*e == was || *e == macro::kNever) continue;
                            const int64_t t = (int64_t)*e + dir;
                            fine = fine && t >= 1;
                            if (fine) *e = (Tick)t;
                        }
                        if (fine && (qh[i].press != ch[i].press || qh[i].release != ch[i].release))
                            fine = holdOrdered(qh, i) && (!i || holdOrdered(qh, i - 1));
                    }
                    if (fine && attempt(q, fast).ok) {
                        r.result = Shift::Passed;
                        r.plan = std::move(q);
                        r.followed = true;
                        ++out.predicted;
                    }
                }
                if (r.result != Shift::Passed) {
                    r = shiftEdge(cur, {w.player, w.hold, w.press}, dir * (shift + 1), tryPlan);
                    out.retimed += r.retimed;
                }
                if (r.result == Shift::Blocked) limited = true;
                if (r.result == Shift::Failed && r.death)
                    death = (int)((int64_t)r.death - ((int64_t)w.tick + dir * (shift + 1)));
                if (r.result != Shift::Passed) break;
                followed.push_back(r.followed);
                passed.push_back(std::move(r.plan));
                ++shift;
            }
            if (std::find(followed.begin(), followed.begin() + shift, true) != followed.begin() + shift)
                ++out.followed;
            passed.resize(shift);
            (dir < 0 ? w.early : w.late) = shift;
            (dir < 0 ? w.earlyLimited : w.lateLimited) = limited;
            (dir < 0 ? w.earlyDeath : w.lateDeath) = limited ? -1 : death;
        }
        // Played mid-window from here on, as a player would.
        const int d = (w.late - w.early) / 2;
        if (o.center && finished && d != 0) {
            if (rebase(d < 0 ? bounds[0][-d - 1] : bounds[1][d - 1])) {
                ++out.centered;
                w.tick = (Tick)((int64_t)w.tick + d);
                w.early += d;
                w.late -= d;
            }
        }
        out.edges.push_back(w);
    }

    if (o.removals) {
        size_t n = 0, total = cur.presses();
        for (int ch = 0; ch < 2; ++ch) {
            out.removable[ch].assign(cur.channel(ch).size(), false);
            for (size_t i = 0; i < cur.channel(ch).size(); ++i) {
                engine.status("removal " + std::to_string(++n) + "/" + std::to_string(total));
                engine.progress(0.9 + 0.1 * (double)n / (double)std::max<size_t>(1, total));
                Plan p = cur;
                p.channel(ch).erase(p.channel(ch).begin() + (ptrdiff_t)i);
                out.removable[ch][i] = passes(p, fast);
            }
        }
    }
    // The measured plan, from the start.
    out.plan = cur;
    if (finished) {
        engine.status("final replay");
        engine.reset();
        out.verified = engine.run(cur, out.goal).outcome == Outcome::Finished;
    }
    out.rejectedSnapshots = tl.rejected();
    out.snapshotsTrusted = tl.enabled();
    out.complete = true;
}

} // namespace kpsim::search
