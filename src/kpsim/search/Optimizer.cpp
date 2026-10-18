#include "Optimizer.hpp"
#include "EdgeShift.hpp"
#include "InputRules.hpp"
#include "Timeline.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace kpsim::search {
namespace {

constexpr Tick kFinishSlack = 2400;

template <class... A> std::string fmt(const char* f, A... a) {
    char b[256];
    std::snprintf(b, sizeof(b), f, a...);
    return b;
}
constexpr Tick kFar = 120;        // as the frame windows: alive this long after the change...
constexpr int kFarInputs = 2;     // ... past this many untouched inputs, a shift is correctable

class Optimizer {
public:
    Optimizer(Engine& e, const OptimizeOptions& o, OptimizeResult& out) : m_e(e), m_o(o), m_out(out), m_tl(e, 240) {}

    void run(const Plan& start) {
        m_plan = start;
        m_out.clicksBefore = start.presses();
        m_out.inputsBefore = start.inputs();
        m_e.status("base run");
        m_e.reset();
        RunResult r = m_tl.advance(m_plan, 240u * 60u * 30u, &m_nearTrace, &m_looseTrace);
        m_out.plan = m_plan;
        m_out.finishedBefore = r.outcome == Outcome::Finished;
        if (!m_out.finishedBefore) return;
        m_goal = r.tick + kFinishSlack;
        refreshKnown();
        m_history.push_back({m_plan, 0, 0, {}});

        stages();
        for (int attempt = 0;; ++attempt) {
            m_out.violations = violations();
            m_stage = 5;
            m_e.status("verifying from the start");
            advance(0, 1);
            if (replays(m_plan, true)) {
                m_out.verified = true;
                m_out.plan = m_plan;
                return;
            }
            if (m_history.size() < 2) return;
            // The game refused one of the changes: drop it, then redo the later ones that still apply.
            const size_t bad = firstRefused();
            const std::vector<Step> later(m_history.begin() + (ptrdiff_t)bad, m_history.end());
            const Counters last = counters();
            m_banned.push_back({later[0].first, later[0].last});
            ++m_out.rejected;
            restoreCounters(later[0].counters);
            m_plan = m_history[bad - 1].plan;
            m_history.resize(bad);
            if (attempt + 1 >= m_o.retries) {
                m_out.verified = replays(m_plan, true);
                if (m_out.verified) m_out.plan = m_plan;
                return;
            }
            rebuild();
            for (size_t k = 1; k < later.size(); ++k) {
                m_e.status("the game refused a change at " + std::to_string(later[0].first) + ": redoing the next ones " +
                           std::to_string(k) + "/" + std::to_string(later.size() - 1));
                Plan cand;
                if (!applyChange(later[k - 1].plan, later[k].plan, later[k].first, later[k].last, cand)) continue;
                if (tryAdopt(cand)) addCounters(later[k].counters, k + 1 < later.size() ? later[k + 1].counters : last);
            }
        }
    }

private:
    void stages() {
        m_stage = 0;
        humanize();
        m_stage = 1;
        removals();
        m_stage = 2;
        merges();
        m_stage = 3;
        removals();
        // More rounds while they keep taking inputs away.
        for (int round = 1; round < m_o.rounds; ++round) {
            const size_t before = m_plan.inputs();
            merges();
            removals();
            if (m_o.transfers) transfers();
            if (m_o.holdToEnd) holdToEnd();
            if (m_plan.inputs() == before) break;
        }
        m_stage = 4;
        if (m_o.center) center();
        if (m_o.roomy) widen();
    }

    // A run of the whole plan from the start; `strict`: one step per frame, as a player gets.
    bool replays(const Plan& p, bool strict) {
        m_e.reset();
        if (strict) m_e.setStepsPerUpdate(1);
        RunResult v = m_e.run(p, m_goal);
        m_e.setStepsPerUpdate(0);
        return v.outcome == Outcome::Finished;
    }

    // The first change in the history after which the plan no longer replays (bisection).
    size_t firstRefused() {
        m_e.status("finding the change the game refused");
        // Fast runs when they show it, else the strict ones that did.
        const bool strict = replays(m_history.back().plan, false);
        size_t good = 0, bad = m_history.size() - 1;
        while (bad - good > 1) {
            const size_t mid = (good + bad) / 2;
            (replays(m_history[mid].plan, strict) ? good : bad) = mid;
        }
        return bad;
    }

    // Timeline and known states again, for the current plan.
    void rebuild() {
        m_tl.clear();
        m_nearTrace.clear();
        m_looseTrace.clear();
        m_e.reset();
        m_tl.advance(m_plan, m_goal, &m_nearTrace, &m_looseTrace);
        refreshKnown();
        m_changes.clear();
        m_dead.clear();
    }

    struct Counters {
        size_t removed = 0, merged = 0, centered = 0, humanized = 0, widened = 0;
    };
    Counters counters() const { return {m_out.removed, m_out.merged, m_out.centered, m_out.humanized, m_out.widened}; }
    void restoreCounters(const Counters& c) {
        m_out.removed = c.removed;
        m_out.merged = c.merged;
        m_out.centered = c.centered;
        m_out.humanized = c.humanized;
        m_out.widened = c.widened;
    }
    // What one change counted, counted again.
    void addCounters(const Counters& before, const Counters& after) {
        m_out.removed += after.removed - before.removed;
        m_out.merged += after.merged - before.merged;
        m_out.centered += after.centered - before.centered;
        m_out.humanized += after.humanized - before.humanized;
        m_out.widened += after.widened - before.widened;
    }

    // `to` was `from` with the inputs in [first, last] changed: the same change on the current plan,
    // if its holds there are still those of `from`.
    bool applyChange(const Plan& from, const Plan& to, Tick first, Tick last, Plan& out) const {
        auto touches = [&](const Hold& h) { return h.press <= last && h.release >= first; };
        out = m_plan;
        for (int ch = 0; ch < 2; ++ch) {
            Holds was, here, keep;
            for (const auto& h : from.channel(ch))
                if (touches(h)) was.push_back(h);
            for (const auto& h : m_plan.channel(ch)) (touches(h) ? here : keep).push_back(h);
            if (was != here) return false;
            for (const auto& h : to.channel(ch))
                if (touches(h)) keep.push_back(h);
            std::sort(keep.begin(), keep.end(), [](const Hold& a, const Hold& b) { return a.press < b.press; });
            out.channel(ch) = std::move(keep);
        }
        return out.valid() && !(out == m_plan);
    }

    // Two inputs less with the same time held around them: a hold's ticks go to a neighbour hold, or
    // a gap's ticks come off the holds it joins. In the wave (and nearly in the ship) the player
    // ends where it did, by another path.
    void transfers() {
        for (int ch = 1; ch >= 0; --ch)
            for (size_t i = m_plan.channel(ch).size(); i-- > 0;) {
                if ((i & 7) == 0) {
                    status("fewer inputs");
                    advance(m_plan.channel(ch).size() - i, m_plan.channel(ch).size());
                }
                const Holds& h = m_plan.channel(ch);
                if (h[i].release == macro::kNever) continue;
                const Tick held = h[i].release - h[i].press;
                const Tick gap = i + 1 < h.size() ? h[i + 1].press - h[i].release : 0;
                // kind, edit: the hold into the previous / next one, the gap after it off either end.
                const std::pair<int, std::function<bool(Holds&)>> edits[] = {
                    {2, [&](Holds& q) { if (!i) return false; q[i - 1].release += held; q.erase(q.begin() + (ptrdiff_t)i); return true; }},
                    {3, [&](Holds& q) { if (i + 1 >= q.size()) return false; q[i + 1].press -= held; q.erase(q.begin() + (ptrdiff_t)i); return true; }},
                    {4, [&](Holds& q) {
                         if (!gap) return false;
                         q[i].release = q[i + 1].release; q.erase(q.begin() + (ptrdiff_t)i + 1);
                         if (q[i].press + gap >= q[i].release) return false;
                         q[i].press += gap;
                         return true;
                     }},
                    {5, [&](Holds& q) {
                         if (!gap || q[i + 1].release == macro::kNever) return false;
                         q[i].release = q[i + 1].release - gap; q.erase(q.begin() + (ptrdiff_t)i + 1);
                         return q[i].press < q[i].release;
                     }},
                };
                for (const auto& [kind, edit] : edits) {
                    const Key key{kind, ch, h[i].press};
                    if (failedBefore(key)) continue;
                    Plan p = m_plan;
                    if (!edit(p.channel(ch)) || !p.valid()) continue;
                    Span tried;
                    if (tryAdopt(p, &tried)) {
                        ++m_out.merged;
                        break;
                    }
                    m_dead[key] = {tried, m_changes.size()};
                }
            }
    }

    // Holding the last press of each button to the end: one release less.
    void holdToEnd() {
        for (int ch = 0; ch < 2; ++ch) {
            const Holds& h = m_plan.channel(ch);
            if (h.empty() || h.back().release == macro::kNever) continue;
            Plan p = m_plan;
            p.channel(ch).back().release = macro::kNever;
            tryAdopt(p);
        }
    }

    // States of the current plan's run, exact, near and loose (in flight): from any of them, the
    // plan finishes. The final replay catches a near one that was not.
    void refreshKnown() {
        m_known.clear();
        m_nearKnown.clear();
        m_looseKnown.clear();
        const auto& tr = m_tl.trace();
        for (Tick t = 1; t <= tr.size(); ++t)
            if (tr[t - 1]) m_known[stateKey(t, tr[t - 1])] = macro::kNever;
        for (Tick t = 1; t <= m_nearTrace.size(); ++t)
            if (m_nearTrace[t - 1]) m_nearKnown[stateKey(t, m_nearTrace[t - 1])] = macro::kNever;
        for (Tick t = 1; t <= m_looseTrace.size(); ++t)
            if (m_looseTrace[t - 1]) m_looseKnown[stateKey(t, m_looseTrace[t - 1])] = macro::kNever;
    }
    Trace knownTrace(Tick from) {
        Trace tr{nullptr, &m_known, from};
        tr.nearKnown = &m_nearKnown;
        tr.looseKnown = &m_looseKnown;
        return tr;
    }

    // Ticks a run depended on: the state at `from`, the inputs up to `until`.
    struct Span {
        Tick from = 0, until = macro::kNever;
    };

    // Does `cand`, a variant of the current plan, still finish? If so it becomes the plan.
    bool tryAdopt(const Plan& cand, Span* tried = nullptr, const std::function<bool(const Plan&)>& veto = {}) {
        const Tick first = firstDifference(m_plan, cand);
        if (first == macro::kNever) return false;
        const Tick last = lastDifference(m_plan, cand);
        for (const auto& b : m_banned)
            if (first <= b.second + 2 && last + 2 >= b.first) return false;
        m_tl.seek(first >= 2 ? first - 2 : 0);
        const Tick from = m_e.tick();
        // Exact only: a change adopted on a near rejoin was refused by the final replay 6 times
        // out of 6 on Solar Flare, each costing a bisection and a rebuild.
        Trace tr{&m_rec, &m_known, last};
        std::vector<uint64_t> nearRec, looseRec;
        tr.nearRecord = &nearRec;
        tr.looseRecord = &looseRec;
        RunResult r = m_e.run(cand, m_goal, &tr);
        ++m_out.trials;
        if (tried) *tried = {from, r.outcome == Outcome::Died ? r.tick : macro::kNever};
        const bool passed = (r.outcome == Outcome::Converged && r.end == macro::kNever) || r.outcome == Outcome::Finished;
        if (passed && veto && veto(cand)) return false;
        if (r.outcome == Outcome::Converged && r.end == macro::kNever) {
            // Back on the old run from r.tick: only the stretch before it changes.
            m_tl.splice(cand, from, r.tick, m_rec);
            if (m_nearTrace.size() < r.tick) m_nearTrace.resize(r.tick, 0);
            if (m_looseTrace.size() < r.tick) m_looseTrace.resize(r.tick, 0);
            for (Tick t = std::max<Tick>(from, 1); t < r.tick; ++t) {
                m_nearTrace[t - 1] = t <= nearRec.size() ? nearRec[t - 1] : 0;
                m_looseTrace[t - 1] = t <= looseRec.size() ? looseRec[t - 1] : 0;
            }
            m_changes.push_back({from, r.tick});
        } else if (r.outcome == Outcome::Finished) {
            m_tl.truncate(from);
            m_tl.seek(from);
            if (m_nearTrace.size() > m_e.tick()) m_nearTrace.resize(m_e.tick());
            if (m_looseTrace.size() > m_e.tick()) m_looseTrace.resize(m_e.tick());
            if (m_tl.advance(cand, m_goal, &m_nearTrace, &m_looseTrace).outcome != Outcome::Finished) return false;
            m_changes.push_back({from, macro::kNever});
        } else {
            return false;
        }
        m_history.push_back({cand, first, last, counters()});
        m_plan = cand;
        refreshKnown();
        return true;
    }

    // Has an adopted change since `since` touched `s`? If not, a run over `s` ends the same.
    bool changedSince(size_t since, const Span& s) const {
        for (size_t k = since; k < m_changes.size(); ++k)
            if (m_changes[k].from <= s.until && m_changes[k].until >= s.from) return true;
        return false;
    }

    // Is there room for `cand`, as the frame windows count it (rejoins the run, or still alive
    // long after the change)? The plan does not change; tryAdopt checks it fully.
    Try probe(const Plan& cand) {
        const Tick first = firstDifference(m_plan, cand);
        if (first == macro::kNever) return {true};
        const Tick last = lastDifference(m_plan, cand);
        m_tl.seek(first >= 2 ? first - 2 : 0);
        Trace tr = knownTrace(last);
        const Tick until = settledAt(cand, last);
        RunResult r = m_e.run(cand, until, &tr);
        ++m_out.trials;
        if (r.outcome == Outcome::Converged) return {r.end == macro::kNever, r.end};
        if (r.outcome == Outcome::Reached && until < m_goal) return {true, r.tick};
        return {r.outcome == Outcome::Finished, r.tick};
    }
    Tick settledAt(const Plan& p, Tick last) const {
        std::vector<Tick> after;
        for (const Holds* h : {&p.holds, &p.holds2})
            for (const Hold& x : *h)
                for (Tick e : {x.press, x.release})
                    if (e > last && e != macro::kNever) after.push_back(e);
        if (after.size() < (size_t)kFarInputs) return m_goal;
        std::nth_element(after.begin(), after.begin() + (kFarInputs - 1), after.end());
        return std::min<Tick>(m_goal, std::max<Tick>(last + kFar, after[kFarInputs - 1]) + 1);
    }
    bool finishes(const Plan& cand) { return probe(cand).ok; }

    // Hold i and its gaps keep the human minimums.
    bool humanOk(const Holds& h, size_t i) const {
        const auto& r = m_o.rules;
        if (h[i].release != macro::kNever && h[i].release - h[i].press < r.minPress) return false;
        if (i && h[i].press - h[i - 1].release < r.minRelease) return false;
        if (i + 1 < h.size() && h[i + 1].press - h[i].release < r.minRelease) return false;
        if (i && h[i].press - h[i - 1].press < r.minInterval) return false;
        if (i + 1 < h.size() && h[i + 1].press - h[i].press < r.minInterval) return false;
        return true;
    }

    size_t violations() const {
        size_t n = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < m_plan.channel(ch).size(); ++i) n += !humanOk(m_plan.channel(ch), i);
        return n;
    }

    // A press, a gap or a click rate a human cannot play: stretched, merged or dropped,
    // whichever still finishes.
    void humanize() {
        const auto& r = m_o.rules;
        if (!r.minPress && !r.minRelease && !r.minInterval) return;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < m_plan.channel(ch).size(); ++i) {
                if (humanOk(m_plan.channel(ch), i)) continue;
                if ((i & 7) == 0) {
                    status("human inputs");
                    advance(i, m_plan.channel(ch).size());
                }
                const Hold h = m_plan.channel(ch)[i];
                std::vector<Plan> tries;
                auto add = [&](auto&& edit) {
                    Plan p = m_plan;
                    edit(p.channel(ch));
                    tries.push_back(std::move(p));
                };
                const Holds& cur = m_plan.channel(ch);
                if (i && (h.press - cur[i - 1].release < r.minRelease || h.press - cur[i - 1].press < r.minInterval))
                    add([&](Holds& q) {   // one hold with the previous
                        q[i - 1].release = q[i].release;
                        q.erase(q.begin() + (ptrdiff_t)i);
                    });
                if (h.release != macro::kNever && h.release - h.press < r.minPress) {
                    add([&](Holds& q) { q[i].release = q[i].press + r.minPress; });
                    if (h.release > r.minPress) add([&](Holds& q) { q[i].press = q[i].release - r.minPress; });
                }
                if (i + 1 < cur.size() &&
                    (cur[i + 1].press - h.release < r.minRelease || cur[i + 1].press - h.press < r.minInterval))
                    add([&](Holds& q) {   // one hold with the next
                        q[i].release = q[i + 1].release;
                        q.erase(q.begin() + (ptrdiff_t)i + 1);
                    });
                add([&](Holds& q) { q.erase(q.begin() + (ptrdiff_t)i); });
                for (auto& p : tries) {
                    if (p.valid() && tryAdopt(p)) {
                        ++m_out.humanized;
                        if (i) --i;   // look again around the change
                        break;
                    }
                }
            }
    }

    void removals() {
        for (int ch = 1; ch >= 0; --ch)
            for (size_t i = m_plan.channel(ch).size(); i-- > 0;) {
                if ((i & 7) == 0) {
                    status("removing clicks");
                    advance(m_plan.channel(ch).size() - i, m_plan.channel(ch).size());
                }
                const Key key{0, ch, m_plan.channel(ch)[i].press};
                if (failedBefore(key)) continue;
                Plan p = m_plan;
                p.channel(ch).erase(p.channel(ch).begin() + (ptrdiff_t)i);
                Span tried;
                if (tryAdopt(p, &tried, narrows())) ++m_out.removed;
                else m_dead[key] = {tried, m_changes.size()};
            }
    }

    // Edit kind (0 remove, 1 merge with the previous hold), button, press of the hold.
    using Key = std::tuple<int, int, Tick>;
    // Already failed, and nothing it ran over has changed since.
    bool failedBefore(const Key& key) const {
        auto it = m_dead.find(key);
        return it != m_dead.end() && !changedSince(it->second.second, it->second.first);
    }

    void merges() {
        for (int ch = 1; ch >= 0; --ch)
            for (size_t i = m_plan.channel(ch).size(); i-- > 1;) {
                const Holds& h = m_plan.channel(ch);
                if (h[i].press - h[i - 1].release > m_o.mergeGap) continue;
                if ((i & 7) == 0) {
                    status("merging clicks");
                    advance(m_plan.channel(ch).size() - i, m_plan.channel(ch).size());
                }
                const Key key{1, ch, h[i].press};
                if (failedBefore(key)) continue;
                Plan p = m_plan;
                Holds& q = p.channel(ch);
                q[i - 1].release = q[i].release;
                q.erase(q.begin() + (ptrdiff_t)i);
                Span tried;
                if (tryAdopt(p, &tried, narrows())) ++m_out.merged;
                else m_dead[key] = {tried, m_changes.size()};
            }
    }

    // The plan with this edge moved by `shift` that still finishes, the next inputs re-timed if
    // needed (the rule of the frame windows).
    Shift shifted(int ch, size_t i, bool press, int shift) {
        return shiftEdge(m_plan, {ch, i, press}, shift, [&](const Plan& p) { return probe(p); },
                         [&](const Holds& h, size_t k) { return humanOk(h, k); });
    }
    bool fits(int ch, size_t i, bool press, int shift) { return shifted(ch, i, press, shift).result == Shift::Passed; }

    // How far the edge moves each way and still finishes (up to cap), and the plan at each shift.
    int reach(int ch, size_t i, bool press, int dir, int cap, std::vector<Plan>& plans) {
        while ((int)plans.size() < cap) {
            Shift s = shifted(ch, i, press, dir * ((int)plans.size() + 1));
            if (s.result != Shift::Passed) break;
            plans.push_back(std::move(s.plan));
        }
        return (int)plans.size();
    }

    void center() {
        int before = -1, after = -1;
        auto lower = [](int& v, int w) { v = v < 0 ? w : std::min(v, w); };
        size_t done = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < m_plan.channel(ch).size(); ++i)
                for (bool press : {true, false}) {
                    if (!press && m_plan.channel(ch)[i].release == macro::kNever) continue;
                    if ((++done & 7) == 0) {
                        status("centering timings");
                        advance(done, 2 * m_plan.presses());
                    }
                    // Comfortable room each way: leave it.
                    if (fits(ch, i, press, -m_o.comfort) && fits(ch, i, press, +m_o.comfort)) continue;
                    std::vector<Plan> earlier, later;
                    int early = reach(ch, i, press, -1, m_o.maxShift, earlier);
                    int late = reach(ch, i, press, +1, m_o.maxShift, later);
                    lower(before, early + late + 1);
                    int d = (late - early) / 2;
                    if (d != 0 && tryAdopt(d < 0 ? earlier[-d - 1] : later[d - 1])) {
                        ++m_out.centered;
                        early += d;
                        late -= d;
                    }
                    lower(after, early + late + 1);
                    if (early + late + 1 < 3) {
                        const Hold& now = m_plan.channel(ch)[i];
                        m_tight.push_back({ch, press ? now.press : now.release, press, early + late + 1});
                    }
                }
        m_out.narrowestBefore = before;
        m_out.narrowestAfter = after;
    }

    // ---- room over clicks (OptimizeOptions::roomy) ----

    // A tick each way for this edge of `base`, next inputs re-timed as the windows allow.
    bool roomAt(const Plan& base, int ch, size_t i, bool press) {
        auto ok = [&](int d) {
            return shiftEdge(base, {ch, i, press}, d, [&](const Plan& p) { return probe(p); },
                             [&](const Holds& h, size_t k) { return humanOk(h, k); })
                       .result == Shift::Passed;
        };
        return ok(-1) && ok(+1);
    }
    // Veto for a removal or merge: an input it changed, or its neighbours, left without that room.
    std::function<bool(const Plan&)> narrows() {
        if (!m_o.roomy) return {};
        return [this](const Plan& cand) {
            const Tick first = firstDifference(m_plan, cand), last = lastDifference(m_plan, cand);
            for (int ch = 0; ch < 2; ++ch) {
                const Holds& h = cand.channel(ch);
                for (size_t i = 0; i < h.size(); ++i)
                    for (bool press : {true, false}) {
                        const Tick t = press ? h[i].press : h[i].release;
                        if (t == macro::kNever || t + 60 < first || t > last + 60) continue;
                        if (!roomAt(cand, ch, i, press)) return true;
                    }
            }
            return false;
        };
    }

    // A wave timing left with less than a tick each way, on one long tight line: a zigzag added in
    // the line beside it, as players fly it (Solar Flare 35 s: one 62-tick descent between saws,
    // where everyone climbs once in the middle). A block of L ticks at the other level inside a
    // segment, with the segment's closing edge moved by L plus its opening edge's shift, keeps the
    // time spent at each level: the wave is back on the old route at that edge, exactly.
    void widen() {
        const auto& r = m_o.rules;
        const Tick minP = std::max<Tick>({r.minPress, r.minRelease, (Tick)6});
        const auto tight = m_tight;
        size_t done = 0;
        int left = -1;   // the narrowest still tight
        for (const auto& [ch, t, press, width] : tight) {
            status("room for tight timings");
            advance(++done, tight.size());
            const Holds& h = m_plan.channel(ch);
            size_t i = 0;
            while (i < h.size() && (press ? h[i].press : h[i].release) != t) ++i;
            const size_t x = 2 * i + (press ? 0 : 1);
            const int mode = i < h.size() ? modeAt(ch, t) : -1;
            m_out.widenLog += fmt(" %s@%u (mode %d):", press ? "press" : "release", t, mode);
            if (mode == kWave && (widenSegment(ch, x, minP, true) || (x && widenSegment(ch, x - 1, minP, false)))) {
                ++m_out.widened;
                continue;
            }
            left = left < 0 ? width : std::min(left, width);
        }
        if (!tight.empty()) m_out.narrowestAfter = left < 0 ? 3 : left;
    }

    int modeAt(int ch, Tick t) {
        m_tl.seek(t > 2 ? t - 2 : 0);
        if (m_e.tick() + 1 < t) m_e.run(m_plan, t - 1);
        return m_e.mode(ch);
    }

    // Segment [e[s], e[s+1]) of channel ch's edges: a block of L ticks at the other level from tick
    // a, the opening edge moved by dE, the next k-1 edges by D = dE + L; the segment before e[s+k]
    // gives the D ticks back, so the wave rejoins the route there. Every tick is tried: the wave
    // flies straight lines (by dE before a, dE - a in the block, D after it), so a death on one
    // cuts every try crossing it there. Of the tries that pass, the ones deepest inside the passing
    // set first, as a player aims for the middle of the gap.
    bool widenSegment(int ch, size_t s, Tick minP, bool tightOpens) {
        std::vector<Tick> e;
        for (const Hold& x : m_plan.channel(ch)) {
            e.push_back(x.press);
            e.push_back(x.release);
        }
        const int64_t never = macro::kNever;
        auto at = [&](size_t j) { return j < e.size() ? (int64_t)e[j] : never; };
        if (s + 1 >= e.size() || at(s + 1) == never || at(s + 1) - at(s) < 2 * (int64_t)minP + 1) return false;
        struct Cand {
            int dE, L, k;
            int64_t a;
            int cost;
            auto key() const { return std::make_tuple(dE, L, k, a); }
        };
        std::vector<Cand> cands;
        for (int k : {2, 4})
            for (int dE = -10; dE <= 10; ++dE)
                for (int L = (int)minP; L <= (int)minP + 24; ++L) {
                    const int64_t D = dE + L, open = at(s) + dE, close = at(s + 1) + D;
                    if (open < 1 || (s && open < at(s - 1) + minP) || at(s + k - 1) == never) continue;
                    if (at(s + k) != never && at(s + k - 1) + D + minP > at(s + k)) continue;
                    const int64_t lo = open + minP, hi = close - L - minP;   // where the block may start
                    for (int64_t a = lo; a <= hi; ++a)
                        cands.push_back({dE, L, k, a, std::abs(dE) + (int)std::abs(a - (lo + hi) / 2) / 2 + (k > 2 ? 4 : 0)});
                }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.cost < y.cost; });
        auto build = [&](const Cand& c) {
            const int64_t D = c.dE + c.L;
            std::vector<Tick> f(e.begin(), e.begin() + (ptrdiff_t)s);
            f.insert(f.end(), {(Tick)(at(s) + c.dE), (Tick)c.a, (Tick)(c.a + c.L)});
            for (size_t j = s + 1; j < s + (size_t)c.k; ++j) f.push_back((Tick)(at(j) + D));
            f.insert(f.end(), e.begin() + (ptrdiff_t)(s + c.k), e.end());
            Plan p = m_plan;
            Holds& q = p.channel(ch);
            q.clear();
            for (size_t j = 0; j + 1 < f.size(); j += 2) q.push_back({f[j], f[j + 1]});
            return p;
        };
        // Deaths on each line: before the block (by dE), in it (dE - a), after it (D).
        std::map<int, int64_t> openDeath;
        std::map<int64_t, std::vector<int64_t>> blockDeaths, backDeaths;
        auto cut = [&](const Cand& c) {
            const int64_t close = at(s + 1) + c.dE + c.L;
            if (auto it = openDeath.find(c.dE); it != openDeath.end() && c.a > it->second + 1) return true;
            if (auto it = blockDeaths.find(c.dE - c.a); it != blockDeaths.end())
                for (int64_t d : it->second)
                    if (c.a + 1 < d && d + 1 < c.a + c.L) return true;
            if (auto it = backDeaths.find(c.dE + c.L); it != backDeaths.end())
                for (int64_t d : it->second)
                    if (c.a + c.L + 1 < d && d + 1 < close) return true;
            return false;
        };
        std::map<std::tuple<int, int, int, int64_t>, size_t> index, passing;   // key -> index in cands
        for (size_t i = 0; i < cands.size(); ++i) index[cands[i].key()] = i;
        std::vector<char> seen(cands.size(), 0);
        int probes = 0, cuts = 0, roomless = 0, refused = 0, grown = 0, wrongCuts = 0;
        auto note = [&](const std::string& why) {
            m_out.widenLog += fmt(" [seg %u-%u: %zu tries, %d cut, %d probed, %zu passed (%d grown, %d cut wrongly), %d without room, %d refused",
                                  e[s], e[s + 1], cands.size(), cuts, probes, passing.size(), grown, wrongCuts, roomless,
                                  refused) + why + "]";
        };
        // `growing`: around a passing try, played even when cut, to check the cut.
        auto tryOne = [&](size_t i, bool growing) {
            if (seen[i]) return;
            seen[i] = 'h';
            const Cand& c = cands[i];
            Plan p = build(c);
            bool human = p.valid();
            for (size_t j = 0; human && j < p.channel(ch).size(); ++j) human = humanOk(p.channel(ch), j);
            if (!human) return;
            const bool wasCut = cut(c);
            if (wasCut && !growing) {
                ++cuts;
                seen[i] = 'c';
                return;
            }
            ++probes;
            const Try r = probe(p);
            seen[i] = r.ok ? 'P' : 'x';
            if (r.ok) {
                passing[c.key()] = i;
                grown += growing;
                wrongCuts += wasCut;
                return;
            }
            const int64_t d = r.death, close = at(s + 1) + c.dE + c.L;
            if (d < c.a) {
                auto [it, fresh] = openDeath.try_emplace(c.dE, d);
                if (!fresh) it->second = std::min(it->second, d);
            } else if (d < c.a + c.L) {
                blockDeaths[c.dE - c.a].push_back(d);
            } else if (d < close) {
                backDeaths[c.dE + c.L].push_back(d);
            }
        };
        // A grid of every other tick over the whole line, then the passing set followed tick by tick
        // from what passed, to its middle.
        for (size_t i = 0; i < cands.size() && probes < 300; ++i) {
            const Cand& c = cands[i];
            const int64_t lo = at(s) + c.dE + minP;
            if (c.dE % 2 == 0 && (c.L - (int)minP) % 2 == 0 && (c.a - lo) % 2 == 0) tryOne(i, false);
        }
        // One of the three lines moved by a tick, the others kept: the opening edge with the block
        // (dE, a), the block alone (a), its end with the closing edge (L). A player's timing a tick
        // off, the next ones re-timed, is one of these.
        auto around = [](const std::tuple<int, int, int, int64_t>& key) {
            const auto [dE, L, k, a] = key;
            return std::array{std::make_tuple(dE - 1, L + 1, k, a - 1), std::make_tuple(dE + 1, L - 1, k, a + 1),
                              std::make_tuple(dE, L, k, a - 1), std::make_tuple(dE, L, k, a + 1),
                              std::make_tuple(dE, L - 1, k, a), std::make_tuple(dE, L + 1, k, a)};
        };
        std::vector<size_t> grow;
        for (const auto& [key, i] : passing) grow.push_back(i);
        std::sort(grow.begin(), grow.end());   // cands are in cost order
        for (size_t g = 0; g < grow.size() && probes < 900; ++g)
            for (const auto& o : around(cands[grow[g]].key())) {
                auto it = index.find(o);
                if (it == index.end() || seen[it->second] || probes >= 900) continue;
                tryOne(it->second, true);
                if (passing.count(o)) grow.push_back(it->second);
            }
        // Depth in the passing set: lines moved a tick at a time to the nearest try that fails.
        std::map<std::tuple<int, int, int, int64_t>, int> depth;
        for (const auto& [key, i] : passing) depth[key] = 1 << 20;
        for (bool changed = true; changed;) {
            changed = false;
            for (auto& [key, dp] : depth) {
                int m = 1 << 20;
                for (const auto& o : around(key)) {
                    auto it = depth.find(o);
                    m = std::min(m, it == depth.end() ? 0 : it->second);
                }
                if (m + 1 < dp) {
                    dp = m + 1;
                    changed = true;
                }
            }
        }
        std::vector<std::pair<int, size_t>> order;
        int minDE = 99, maxDE = -99;
        for (const auto& [key, i] : passing) {
            order.push_back({depth[key], i});
            minDE = std::min(minDE, std::get<0>(key));
            maxDE = std::max(maxDE, std::get<0>(key));
        }
        std::sort(order.begin(), order.end(), [&](const auto& x, const auto& y) {
            return x.first != y.first ? x.first > y.first : cands[x.second].cost < cands[y.second].cost;
        });
        auto ticks = [&](const Cand& c) {
            return fmt("%u %lld-%lld %u", (Tick)(at(s) + c.dE), (long long)c.a, (long long)(c.a + c.L),
                       (Tick)(at(s + 1) + c.dE + c.L));
        };
        int noRoom[4] = {};   // first edge without room: opening, block start, block end, closing
        for (const auto& [n, i] : order) {
            if (roomless >= 24) break;
            const Cand& c = cands[i];
            const Plan p = build(c);
            // The tight edge first, then the rest it touched: a tick each way. Then the whole run.
            const size_t tight = tightOpens ? s : s + 3;
            int failed = roomAt(p, ch, tight / 2, tight % 2 == 0) ? -1 : (int)(tight - s);
            for (size_t j = s; failed < 0 && j < s + 4; ++j)
                if (j != tight && !roomAt(p, ch, j / 2, j % 2 == 0)) failed = (int)(j - s);
            if (failed >= 0) {
                ++roomless;
                ++noRoom[failed];
                continue;
            }
            if (tryAdopt(p)) {
                note(", kept " + ticks(c) + fmt(" (dE %d, k %d, depth %d)", c.dE, c.k, n));
                return true;
            }
            ++refused;
        }
        std::string why;
        if (!order.empty()) {
            std::string around6;   // its neighbours: P passed, x died, c cut, h not human, . not tried, - out of range
            for (const auto& o : around(cands[order[0].second].key())) {
                auto it = index.find(o);
                around6 += it == index.end() ? '-' : seen[it->second] ? seen[it->second] : '.';
            }
            why = ", first " + ticks(cands[order[0].second]) +
                  fmt(" depth %d [%s], dE %d..%d", order[0].first, around6.c_str(), minDE, maxDE);
        }
        if (roomless) why += fmt(", no room at %d/%d/%d/%d", noRoom[0], noRoom[1], noRoom[2], noRoom[3]);
        note(why);
        return false;
    }

    void status(const std::string& what) {
        m_e.status(what + "  clicks " + std::to_string(m_plan.presses()) + "  trials " + std::to_string(m_out.trials));
    }

    // Stage start and share of the whole: humanize, removals, merges, removals, centring, proof.
    void advance(size_t i, size_t n) {
        static const double kStart[] = {0.0, 0.08, 0.38, 0.46, 0.49, 0.95};
        static const double kShare[] = {0.08, 0.30, 0.08, 0.03, 0.46, 0.05};
        m_e.progress(kStart[m_stage] + kShare[m_stage] * (double)i / (double)std::max<size_t>(1, n));
    }

    Engine& m_e;
    const OptimizeOptions& m_o;
    OptimizeResult& m_out;
    Timeline m_tl;
    Plan m_plan;
    Tick m_goal = 0;
    int m_stage = 0;
    std::vector<std::tuple<int, Tick, bool, int>> m_tight;   // edges centring left under 3 ticks, their width
    KnownStates m_known, m_nearKnown, m_looseKnown;
    std::vector<uint64_t> m_rec, m_nearTrace, m_looseTrace;
    std::vector<Span> m_changes;   // adopted, in order
    std::map<Key, std::pair<Span, size_t>> m_dead;   // failed edits, and the changes made by then
    // Every adopted plan, the ticks its change touched and the counters before it.
    struct Step {
        Plan plan;
        Tick first, last;
        Counters counters;
    };
    std::vector<Step> m_history;
    std::vector<std::pair<Tick, Tick>> m_banned;   // ticks of changes the final replay refused
};

} // namespace

void optimizePlan(Engine& engine, const Plan& start, const OptimizeOptions& options, OptimizeResult& out) {
    out = OptimizeResult{};
    out.plan = start;
    Optimizer(engine, options, out).run(start);
}

} // namespace kpsim::search
