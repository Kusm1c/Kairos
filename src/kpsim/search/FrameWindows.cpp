#include "FrameWindows.hpp"
#include "EdgeShift.hpp"
#include "Timeline.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace kpsim::search {
namespace {

constexpr Tick kLimit = 240u * 60u * 30u;   // 30 min
constexpr Tick kFinishSlack = 2400;
constexpr Tick kLearn = 240;
constexpr int kNarrow = 2;   // windows this narrow look for a wider one nearby
constexpr int kGap = 12;     // ... this many ticks away
constexpr Tick kFar = 120;   // a death this long after the change...
constexpr int kFarInputs = 2;   // ... with this many untouched inputs before it: correctable
constexpr Tick kPinGap = 40;    // a pinned snapshot this much closer than the last one

size_t breaches(const InputRules& r, const Plan& p) {
    size_t n = 0;
    for (const Holds* h : {&p.holds, &p.holds2})
        for (size_t i = 0; i < h->size(); ++i) {
            const Hold& x = (*h)[i];
            n += x.release != macro::kNever && x.release - x.press < r.minPress;
            if (i) n += (x.press - (*h)[i - 1].release < r.minRelease) + (x.press - (*h)[i - 1].press < r.minInterval);
        }
    return n;
}

} // namespace

void findWindows(Engine& engine, const Plan& plan, const WindowOptions& o, WindowReport& out) {
    out = WindowReport{};
    out.cap = o.maxShift;
    Timeline tl(engine, o.spacing);

    engine.status("base run");
    engine.progress(0.0);
    engine.reset();
    std::vector<uint64_t> nearTrace, looseTrace;
    out.base = tl.advance(plan, kLimit, &nearTrace, &looseTrace);
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
    KnownStates known, nearKnown, looseKnown;
    for (Tick t = 1; t <= looseTrace.size(); ++t)
        if (looseTrace[t - 1]) looseKnown[stateKey(t, looseTrace[t - 1])] = macro::kNever;
    bool flying = false;   // the edge being measured is in flight
    const auto& trace = tl.trace();
    for (Tick t = 1; t <= trace.size(); ++t)
        if (trace[t - 1]) known[stateKey(t, trace[t - 1])] = macro::kNever;
    for (Tick t = 1; t <= nearTrace.size(); ++t)
        if (nearTrace[t - 1]) nearKnown[stateKey(t, nearTrace[t - 1])] = macro::kNever;
    std::vector<uint64_t> rec;
    const bool fast = !o.strict;
    Plan cur = plan;   // centred as edges are measured
    bool rejoinedAtOnce = false;   // the last attempt was back on the run within two ticks of its change
    int pinId = 0;     // Timeline::pin near the edge
    Tick pinTick = 0;
    auto humanOk = [&](const Plan& p) { return breaches(o.rules, p) <= breaches(o.rules, cur); };

    // Died long after the change, past inputs a player would correct with.
    auto lateDeath = [&](const Plan& p, Tick death) {
        const Tick last = lastDifference(cur, p);
        if (death == macro::kNever || death <= last + kFar) return false;
        int n = 0;
        for (const Holds* h : {&p.holds, &p.holds2})
            for (const Hold& x : *h)
                n += (x.press > last && x.press < death) + (x.release > last && x.release < death);
        if (n < kFarInputs) return false;
        ++out.farDeaths;
        return true;
    };
    // Alive past this tick, a try passes whatever comes after (lateDeath): no need to run on.
    auto settledAt = [&](const Plan& p) -> Tick {
        if (o.strict) return out.goal;
        const Tick last = lastDifference(cur, p);
        std::vector<Tick> after;
        for (const Holds* h : {&p.holds, &p.holds2})
            for (const Hold& x : *h)
                for (Tick e : {x.press, x.release})
                    if (e > last && e != macro::kNever) after.push_back(e);
        if (after.size() < (size_t)kFarInputs) return out.goal;
        std::nth_element(after.begin(), after.begin() + (kFarInputs - 1), after.end());
        return std::min<Tick>(out.goal, std::max<Tick>(last + kFar, after[kFarInputs - 1]) + 1);
    };
    auto attempt = [&](const Plan& p, bool useKnown) -> Try {
        Tick c = firstDifference(cur, p);
        if (c == macro::kNever) return {true};
        Tick last = lastDifference(cur, p);
        tl.seek(c >= 2 ? c - 2 : 0);
        Tick from = engine.tick();
        Trace tr{&rec, useKnown ? &known : nullptr, last};
        if (useKnown) tr.nearKnown = &nearKnown;
        if (useKnown && flying) tr.looseKnown = &looseKnown;
        const Tick until = settledAt(p);
        RunResult r = engine.run(p, until, &tr);
        ++out.trials;
        rejoinedAtOnce = r.outcome == Outcome::Converged && r.end == macro::kNever && r.tick <= last + 2;
        if (r.outcome == Outcome::Converged) {
            ++out.pruned;
            return {r.end == macro::kNever || lateDeath(p, r.end), r.end};
        }
        if (r.outcome == Outcome::Reached && until < out.goal) {
            ++out.farDeaths;
            return {true, r.tick};
        }
        bool ok = finished ? r.outcome == Outcome::Finished : r.outcome == Outcome::Reached;
        if (!ok && r.outcome == Outcome::Died && lateDeath(p, r.tick)) return {true, r.tick};
        if (!ok && useKnown && r.outcome == Outcome::Died) {
            Tick a = std::max<Tick>({last, from, 1});
            Tick b = std::min<Tick>({r.tick - 1, a + kLearn, (Tick)rec.size()});
            for (Tick t = a; t <= b; ++t)
                if (rec[t - 1]) known.emplace(stateKey(t, rec[t - 1]), r.tick);
        }
        return {ok, r.tick};
    };
    auto passes = [&](const Plan& p, bool useKnown) { return attempt(p, useKnown).ok; };
    // p is back on the run, exactly, within two ticks of its last change: the change did nothing.
    auto unchanged = [&](const Plan& p) {
        const Tick c = firstDifference(cur, p), last = lastDifference(cur, p);
        tl.seek(c >= 2 ? c - 2 : 0);
        Trace tr{nullptr, &known, last};
        RunResult r = engine.run(p, std::min<Tick>(out.goal, last + 3), &tr);
        ++out.trials;
        return r.outcome == Outcome::Converged && r.end == macro::kNever;
    };

    // p becomes the measured plan. Run until it rejoins the old run (spliced in), else to the end.
    auto rebase = [&](const Plan& p) {
        const Tick c = firstDifference(cur, p);
        tl.seek(c >= 2 ? c - 2 : 0);
        const Tick from = engine.tick();
        std::vector<uint64_t> exact, nearRec, looseRec;
        Trace tr{&exact, &known, lastDifference(cur, p)};
        tr.nearRecord = &nearRec;
        tr.nearKnown = &nearKnown;
        tr.looseRecord = &looseRec;
        RunResult r = engine.run(p, out.goal, &tr);
        ++out.trials;
        if (r.outcome == Outcome::Converged && r.end == macro::kNever) {
            tl.splice(p, from, r.tick, exact);
            if (nearTrace.size() < r.tick) nearTrace.resize(r.tick, 0);
            if (looseTrace.size() < r.tick) looseTrace.resize(r.tick, 0);
            for (Tick t = std::max<Tick>(from, 1); t < r.tick; ++t) {
                if (t <= exact.size() && exact[t - 1]) known[stateKey(t, exact[t - 1])] = macro::kNever;
                if (t <= nearRec.size() && nearRec[t - 1]) {
                    nearTrace[t - 1] = nearRec[t - 1];
                    nearKnown[stateKey(t, nearRec[t - 1])] = macro::kNever;
                }
                looseTrace[t - 1] = t <= looseRec.size() ? looseRec[t - 1] : 0;
                if (looseTrace[t - 1]) looseKnown[stateKey(t, looseTrace[t - 1])] = macro::kNever;
            }
            ++out.splices;
        } else if (r.outcome == Outcome::Finished) {
            tl.truncate(from);
            tl.seek(from);
            if (nearTrace.size() > engine.tick()) nearTrace.resize(engine.tick());
            if (looseTrace.size() > engine.tick()) looseTrace.resize(engine.tick());
            if (tl.advance(p, kLimit, &nearTrace, &looseTrace).outcome != Outcome::Finished) return false;
            known.clear();
            nearKnown.clear();
            looseKnown.clear();
            for (Tick t = 1; t <= looseTrace.size(); ++t)
                if (looseTrace[t - 1]) looseKnown[stateKey(t, looseTrace[t - 1])] = macro::kNever;
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
        for (size_t i = 0; i < plan.channel(ch).size() && (!o.holds || i < o.holds); ++i) {
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
        // Tries restart just before the edge's earliest shift, not from the last spaced snapshot.
        if (fast && w.tick > (Tick)o.maxShift + 2 + pinTick + kPinGap) {
            if (const int id = tl.pin(w.tick - (Tick)o.maxShift - 2)) {
                if (pinId) tl.unpin(pinId);
                pinId = id;
                pinTick = w.tick - (Tick)o.maxShift - 2;
            }
        }
        flying = w.player == 0 && w.tick >= 2 && w.tick - 1 <= looseTrace.size() && looseTrace[w.tick - 2];
        w.flying = flying;
        const ShiftOptions steer = flying ? kSteer : ShiftOptions{};
        auto tryPlan = [&](const Plan& p) { return attempt(p, fast); };
        // From the level start, no snapshot, no pruning: what the game itself says.
        auto fromStart = [&](const Plan& p) -> Try {
            engine.reset();
            const Tick until = settledAt(p);
            RunResult r = engine.run(p, until);
            ++out.trials;
            if (r.outcome == Outcome::Reached && until < out.goal) return {true, r.tick};
            const bool ok = finished ? r.outcome == Outcome::Finished : r.outcome == Outcome::Reached;
            return {ok || (r.outcome == Outcome::Died && lateDeath(p, r.tick)), r.tick};
        };
        std::vector<Plan> bounds[2];   // earlier, later: the plan that passed at each shift
        std::optional<EdgeTest> failed[2];   // ... and the try past them
        for (int dir : {-1, +1}) {
            int shift = 0;
            bool limited = false;
            std::vector<Plan>& passed = bounds[dir > 0];
            std::vector<bool> followed;    // ... with re-timed follow-ups
            int death = -1;
            bool exact = false;   // a snapshot lied on this side: the rest from the level start
            const std::function<Try(const Plan&)> trial = [&](const Plan& p) { return exact ? fromStart(p) : tryPlan(p); };
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
                    if (fine && trial(q).ok) {
                        r.result = Shift::Passed;
                        r.plan = std::move(q);
                        r.followed = true;
                        ++out.predicted;
                    }
                }
                if (r.result != Shift::Passed) {
                    rejoinedAtOnce = false;
                    r = shiftEdge(cur, {w.player, w.hold, w.press}, dir * (shift + 1), trial, {}, steer);
                    out.retimed += r.retimed;
                }
                // An input that changes nothing here (a release in the air): one tick and the whole
                // range both rejoin the run at once, so the side is the range.
                if (r.result == Shift::Passed && shift == 0 && !r.followed && rejoinedAtOnce && !exact && fast &&
                    !flying && o.maxShift > 1) {
                    std::vector<Plan> plans;
                    for (int k = 2; k <= o.maxShift; ++k) {
                        Plan q = cur;
                        Holds& qh = q.channel(w.player);
                        Tick& at = w.press ? qh[w.hold].press : qh[w.hold].release;
                        const int64_t t = (int64_t)at + dir * k;
                        if (t < 1) break;
                        at = (Tick)t;
                        if (!holdOrdered(qh, w.hold)) break;
                        plans.push_back(std::move(q));
                    }
                    if ((int)plans.size() == o.maxShift - 1 && unchanged(plans.back())) {
                        passed.push_back(std::move(r.plan));
                        followed.push_back(false);
                        for (auto& q : plans) {
                            passed.push_back(std::move(q));
                            followed.push_back(false);
                        }
                        shift = o.maxShift;
                        ++out.quick;
                        continue;
                    }
                }
                // No room at all: proven without the pruning, then from the level start.
                if (r.result == Shift::Failed && shift == 0 && !o.strict) {
                    Plan p = cur;
                    Hold& e = p.channel(w.player)[w.hold];
                    Tick& at = w.press ? e.press : e.release;
                    at = (Tick)((int64_t)at + dir);
                    ++out.proven;
                    bool lied = false;
                    if (attempt(p, false).ok) {
                        ++out.pruneLies;
                        lied = true;
                    } else if (fromStart(p).ok) {
                        ++out.restoreLies;
                        exact = lied = true;
                    }
                    if (lied) {
                        r.result = Shift::Passed;
                        r.plan = std::move(p);
                        r.followed = false;
                    }
                }
                if (r.result == Shift::Blocked) limited = true;
                if (r.result == Shift::Failed && r.death)
                    death = (int)((int64_t)r.death - ((int64_t)w.tick + dir * (shift + 1)));
                if (r.result == Shift::Failed && o.keepTests && r.plan.presses())
                    failed[dir > 0] = EdgeTest{dir * (shift + 1), false, r.reached, r.plan};
                if (r.result != Shift::Passed) break;
                followed.push_back(r.followed);
                passed.push_back(std::move(r.plan));
                ++shift;
            }
            if (std::find(followed.begin(), followed.begin() + shift, true) != followed.begin() + shift) {
                ++out.followed;
                if (flying) ++out.steered;
            }
            passed.resize(shift);
            (dir < 0 ? w.early : w.late) = shift;
            (dir < 0 ? w.earlyLimited : w.lateLimited) = limited;
            (dir < 0 ? w.earlyDeath : w.lateDeath) = limited ? -1 : death;
        }
        EdgeTests kept;
        if (o.keepTests && w.early + w.late + 1 <= o.keepTests) {
            kept.tests.push_back({0, true, 0, cur});
            for (int s = 0; s < 2; ++s) {
                for (size_t i = 0; i < bounds[s].size(); ++i)
                    kept.tests.push_back({(s ? 1 : -1) * (int)(i + 1), true, 0, bounds[s][i]});
                if (failed[s]) kept.tests.push_back(std::move(*failed[s]));
            }
        }
        // A press made in the air: GD buffers it (m_jumpBuffered) and jumps later.
        if (w.press && w.tick > 1 && (o.holds || w.early == 0 || w.late == 0)) {
            tl.seek(w.tick - 1);
            engine.run(cur, w.tick - 1);
            const int m = engine.mode(w.player);
            if (m < 0 || m == kCube || m == kBall || m == kRobot || m == kSpider) w.ground = engine.onGround(w.player);
        }
        // The route may sit in a narrow window beside a wider one, past a deadly gap: move there.
        bool moved = false;
        if (o.center && finished && w.early + w.late + 1 <= kNarrow) {
            struct Region {
                int dir = 0, from = 0;   // shifts dir*from .. dir*(from + plans.size() - 1)
                bool limited = false;
                std::vector<Plan> plans;
            } best;
            const EdgeRef ref{w.player, w.hold, w.press};
            for (int dir : {-1, +1}) {
                if (dir < 0 ? w.earlyLimited : w.lateLimited) continue;
                Region r;
                r.dir = dir;
                for (int k = (dir < 0 ? w.early : w.late) + 2; k <= kGap && r.plans.empty(); ++k) {
                    Shift s = shiftEdge(cur, ref, dir * k, tryPlan, {}, steer);
                    out.retimed += s.retimed;
                    if (s.result == Shift::Blocked) break;
                    if (s.result == Shift::Passed) {
                        r.from = k;
                        r.plans.push_back(std::move(s.plan));
                    }
                }
                if (r.plans.empty()) continue;
                while (true) {
                    if ((int)r.plans.size() >= 2 * o.maxShift + 1) { r.limited = true; break; }
                    Shift s = shiftEdge(cur, ref, dir * (r.from + (int)r.plans.size()), tryPlan, {}, steer);
                    out.retimed += s.retimed;
                    if (s.result != Shift::Passed) { r.limited = s.result == Shift::Blocked; break; }
                    r.plans.push_back(std::move(s.plan));
                }
                const size_t have = best.plans.empty() ? (size_t)(w.early + w.late + 1) : best.plans.size();
                if (r.plans.size() > have) best = std::move(r);
            }
            int mid = -1;   // the middle-most plan within the limits
            for (int k = 0, c = (int)best.plans.size() / 2; k <= c && mid < 0; ++k)
                for (int i : {c - k, c + k})
                    if (mid < 0 && i >= 0 && i < (int)best.plans.size() && humanOk(best.plans[(size_t)i])) mid = i;
            if (mid >= 0) {
                if (rebase(best.plans[(size_t)mid])) {
                    ++out.moved;
                    moved = true;
                    w.movedBy = best.dir * (best.from + mid);
                    w.tick = (Tick)((int64_t)w.tick + w.movedBy);
                    const int inward = mid, outward = (int)best.plans.size() - 1 - mid;
                    (best.dir < 0 ? w.early : w.late) = outward;
                    (best.dir < 0 ? w.late : w.early) = inward;
                    (best.dir < 0 ? w.earlyLimited : w.lateLimited) = best.limited;
                    (best.dir < 0 ? w.lateLimited : w.earlyLimited) = false;
                    w.earlyDeath = w.lateDeath = -1;
                }
            }
        }
        // Played mid-window from here on, as a player would.
        if (!kept.tests.empty() && !moved) {
            kept.edge = w;
            out.tests.push_back(std::move(kept));
        }
        // As near the middle as the limits allow.
        int d = o.center && finished && !moved ? (w.late - w.early) / 2 : 0;
        while (d != 0 && !humanOk(d < 0 ? bounds[0][-d - 1] : bounds[1][d - 1])) d += d < 0 ? 1 : -1;
        if (d != 0) {
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
