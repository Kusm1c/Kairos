#include "Optimizer.hpp"
#include "EdgeShift.hpp"
#include "InputRules.hpp"
#include "Timeline.hpp"

#include <algorithm>
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

        if (m_o.only.ch >= 0) {
            m_stage = 4;
            reroute();
        } else {
            stages();
        }
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
        size_t removed = 0, merged = 0, centered = 0, humanized = 0, rebatched = 0;
    };
    Counters counters() const { return {m_out.removed, m_out.merged, m_out.centered, m_out.humanized, m_out.rebatched}; }
    void restoreCounters(const Counters& c) {
        m_out.removed = c.removed;
        m_out.merged = c.merged;
        m_out.centered = c.centered;
        m_out.humanized = c.humanized;
        m_out.rebatched = c.rebatched;
    }
    // What one change counted, counted again.
    void addCounters(const Counters& before, const Counters& after) {
        m_out.removed += after.removed - before.removed;
        m_out.merged += after.merged - before.merged;
        m_out.centered += after.centered - before.centered;
        m_out.humanized += after.humanized - before.humanized;
        m_out.rebatched += after.rebatched - before.rebatched;
    }

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
                }
        m_out.narrowestBefore = before;
        m_out.narrowestAfter = after;
    }

    // ---- Find better route: the inputs before a tight timing re-timed (OptimizeOptions::only) ----

    static constexpr int kBatchCap = 4;      // windows compared up to 2 * this + 1
    // Batches compare many versions: the next inputs re-timed more lightly than the frame windows do.
    static constexpr ShiftOptions kBatchShift{2, 1, 6, 0};

    // Edge x of a button: hold x / 2, its press when x is even.
    static EdgeRef edgeRef(int ch, size_t x) { return {ch, x / 2, x % 2 == 0}; }
    static Tick edgeTick(const Plan& p, int ch, size_t x) {
        const Hold& h = p.channel(ch)[x / 2];
        return x % 2 == 0 ? h.press : h.release;
    }
    Shift shiftedIn(const Plan& p, int ch, size_t x, int d) {
        return shiftEdge(p, edgeRef(ch, x), d, [&](const Plan& c) { return probe(c); },
                         [&](const Holds& h, size_t k) { return humanOk(h, k); }, kBatchShift);
    }
    struct Reach {
        int early = 0, late = 0;
        std::vector<Plan> plans[2];
        int width() const { return early + late + 1; }
    };
    Reach reachIn(const Plan& p, int ch, size_t x, int cap, bool keep) {
        Reach r;
        for (int k = 0; k < 2; ++k) {
            int& n = k ? r.late : r.early;
            while (n < cap) {
                Shift s = shiftedIn(p, ch, x, (k ? 1 : -1) * (n + 1));
                if (s.result != Shift::Passed) break;
                ++n;
                if (keep) r.plans[k].push_back(std::move(s.plan));
            }
        }
        return r;
    }
    // The same on the current plan, remembered until it changes.
    const Reach& reachNow(int ch, size_t x) {
        if (m_reachOf != m_history.size()) {
            m_reach.clear();
            m_reachOf = m_history.size();
        }
        auto it = m_reach.find({ch, x});
        if (it == m_reach.end()) it = m_reach.emplace(std::make_pair(ch, x), reachIn(m_plan, ch, x, kBatchCap, true)).first;
        return it->second;
    }
    // Windows of edges [from, to] in `p`: the narrowest and the mean.
    std::pair<int, double> batchWindows(const Plan* p, int ch, size_t from, size_t to) {
        int lo = 1 << 20;
        double sum = 0;
        for (size_t x = from; x <= to; ++x) {
            const int w = p ? reachIn(*p, ch, x, kBatchCap, false).width() : reachNow(ch, x).width();
            lo = std::min(lo, w);
            sum += w;
        }
        return {lo, sum / (double)(to - from + 1)};
    }

    void reroute() {
        status("finding a better route");
        const auto& [ch, t, press] = m_o.only;
        std::vector<Tick> e;
        for (const Hold& x : m_plan.channel(ch)) {
            e.push_back(x.press);
            if (x.release != macro::kNever) e.push_back(x.release);
        }
        size_t x = 0;
        while (x < e.size() && e[x] != t) ++x;
        if (x == e.size() || x % 2 != (press ? 0u : 1u)) return;
        const size_t from = x >= (size_t)m_o.batch ? x - (size_t)m_o.batch : 0;
        const uint64_t start = m_out.trials;
        auto spent = [&] { return m_out.trials - start >= m_o.batchTrials; };
        const auto [lo0, mean0] = batchWindows(nullptr, ch, from, x);
        m_out.targetBefore = m_out.targetAfter = reachNow(ch, x).width();
        struct Cand {
            int w;       // the tight timing's window
            size_t x;    // its edge now
            size_t to;   // the batch's last edge (an added click after it included)
            Plan plan;
        };
        std::vector<Cand> cands;
        auto consider = [&](Plan p, size_t at, size_t to) {
            ++m_out.routesTried;
            const int w = reachIn(p, ch, at, kBatchCap, false).width();
            if (w > m_out.targetBefore) cands.push_back({w, at, to, std::move(p)});
        };
        for (size_t j = from; j < x && !spent(); ++j) {
            const Reach& r = reachNow(ch, j);
            for (int k = 0; k < 2; ++k) {
                for (size_t d = 0; d < r.plans[k].size() && !spent(); ++d) consider(r.plans[k][d], x, x);
                if ((int)r.plans[k].size() == kBatchCap)
                    for (int d : {6, 8, 10, 12}) {
                        if (spent()) break;
                        Shift s = shiftedIn(m_plan, ch, j, k ? d : -d);
                        if (s.result != Shift::Passed) break;
                        consider(std::move(s.plan), x, x);
                    }
            }
        }
        // A click added in line s (edges s, s + 1), from the batch to the line after the timing.
        const auto& rules = m_o.rules;
        const int minP = (int)std::max<Tick>({rules.minPress, rules.minRelease, (Tick)6});
        struct Add {
            size_t s;
            int dE, L;
            int64_t a;
            int cost;
        };
        std::vector<Add> adds;
        for (size_t s = from; s <= x && s + 1 < e.size(); ++s)
            for (int dE : {0, -4, 4, -8, 8})
                for (int L : {minP, minP + 8, minP + 16}) {
                    const int64_t open = (int64_t)e[s] + dE, lo = open + minP, hi = (int64_t)e[s + 1] + dE - minP;
                    for (int64_t a = lo; a <= hi; a += 6)
                        adds.push_back({s, dE, L, a, std::abs(dE) + (int)std::abs(a + L / 2 - (lo + hi) / 2) / 4 + L / 8});
                }
        std::stable_sort(adds.begin(), adds.end(), [](const Add& a, const Add& b) { return a.cost < b.cost; });
        for (const Add& c : adds) {
            if (spent()) break;
            advance(m_out.trials - start, m_o.batchTrials);
            std::vector<Tick> f(e.begin(), e.begin() + (ptrdiff_t)c.s);
            f.insert(f.end(), {(Tick)((int64_t)e[c.s] + c.dE), (Tick)c.a, (Tick)(c.a + c.L)});
            size_t last = f.size();
            for (size_t k = c.s + 1; k < e.size(); ++k) {
                const bool moved = k == c.s + 1 || (int64_t)e[k] - (int64_t)f.back() < minP;
                f.push_back(moved ? (Tick)((int64_t)e[k] + c.dE + c.L) : e[k]);
                if (moved) last = f.size() - 1;
            }
            Plan p = m_plan;
            Holds& q = p.channel(ch);
            q.clear();
            for (size_t j = 0; j < f.size(); j += 2) q.push_back({f[j], j + 1 < f.size() ? f[j + 1] : macro::kNever});
            bool human = p.valid();
            for (size_t j = 0; human && j < q.size(); ++j) human = humanOk(q, j);
            if (!human) continue;
            Shift r = shiftedIn(p, ch, last, 0);
            if (r.result == Shift::Passed) consider(std::move(r.plan), c.s < x ? x + 2 : x, std::max(x + 2, c.s + 3));
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.w > b.w; });
        if (cands.size() > 6) cands.resize(6);
        const Cand* best = nullptr;
        double bestMean = mean0;
        for (const Cand& c : cands) {
            const auto sc = batchWindows(&c.plan, ch, from, c.to);
            if (sc.first >= lo0 && (!best || sc.second > bestMean ||
                                   (sc.second == bestMean && c.plan.inputs() < best->plan.inputs()))) {
                best = &c;
                bestMean = sc.second;
            }
        }
        if (best && tryAdopt(best->plan)) {
            ++m_out.rebatched;
            m_out.targetAfter = reachNow(ch, best->x).width();
            m_out.batchMean[0] = mean0;
            m_out.batchMean[1] = bestMean;
        }
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
    std::map<std::pair<int, size_t>, Reach> m_reach;   // reachNow's, for the plan after m_reachOf changes
    size_t m_reachOf = 0;
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
