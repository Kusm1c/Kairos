#include "Optimizer.hpp"
#include "InputRules.hpp"
#include "Timeline.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace kpsim::search {
namespace {

constexpr Tick kFinishSlack = 2400;

class Optimizer {
public:
    Optimizer(Engine& e, const OptimizeOptions& o, OptimizeResult& out) : m_e(e), m_o(o), m_out(out), m_tl(e, 240) {}

    void run(const Plan& start) {
        m_plan = start;
        m_out.clicksBefore = start.presses();
        m_e.status("base run");
        m_e.reset();
        RunResult r = m_tl.advance(m_plan, 240u * 60u * 30u);
        m_out.plan = m_plan;
        m_out.finishedBefore = r.outcome == Outcome::Finished;
        if (!m_out.finishedBefore) return;
        m_goal = r.tick + kFinishSlack;
        refreshKnown();

        m_stage = 0;
        humanize();
        m_stage = 1;
        removals();
        m_stage = 2;
        merges();
        m_stage = 3;
        removals();
        m_stage = 4;
        if (m_o.center) center();
        m_stage = 5;
        m_out.violations = violations();

        m_e.status("verifying from the start");
        advance(0, 1);
        m_e.reset();
        m_e.setStepsPerUpdate(1);
        RunResult v = m_e.run(m_plan, m_goal);
        m_e.setStepsPerUpdate(0);
        m_out.verified = v.outcome == Outcome::Finished;
        if (m_out.verified) m_out.plan = m_plan;
    }

private:
    // States of the current plan's run: from any of them, the plan finishes.
    void refreshKnown() {
        m_known.clear();
        const auto& tr = m_tl.trace();
        for (Tick t = 1; t <= tr.size(); ++t)
            if (tr[t - 1]) m_known[stateKey(t, tr[t - 1])] = macro::kNever;
    }

    // Ticks a run depended on: the state at `from`, the inputs up to `until`.
    struct Span {
        Tick from = 0, until = macro::kNever;
    };

    // Does `cand`, a variant of the current plan, still finish? If so it becomes the plan.
    bool tryAdopt(const Plan& cand, Span* tried = nullptr) {
        const Tick first = firstDifference(m_plan, cand);
        if (first == macro::kNever) return false;
        const Tick last = lastDifference(m_plan, cand);
        m_tl.seek(first >= 2 ? first - 2 : 0);
        const Tick from = m_e.tick();
        Trace tr{&m_rec, &m_known, last};
        RunResult r = m_e.run(cand, m_goal, &tr);
        ++m_out.trials;
        if (tried) *tried = {from, r.outcome == Outcome::Died ? r.tick : macro::kNever};
        if (r.outcome == Outcome::Converged && r.end == macro::kNever) {
            // Back on the old run from r.tick: only the stretch before it changes.
            m_tl.splice(cand, from, r.tick, m_rec);
            m_changes.push_back({from, r.tick});
        } else if (r.outcome == Outcome::Finished) {
            m_tl.truncate(from);
            m_tl.seek(from);
            if (m_tl.advance(cand, m_goal).outcome != Outcome::Finished) return false;
            m_changes.push_back({from, macro::kNever});
        } else {
            return false;
        }
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

    // Would `cand` finish? The plan does not change.
    bool finishes(const Plan& cand) {
        const Tick first = firstDifference(m_plan, cand);
        if (first == macro::kNever) return true;
        m_tl.seek(first >= 2 ? first - 2 : 0);
        Trace tr{nullptr, &m_known, lastDifference(m_plan, cand)};
        RunResult r = m_e.run(cand, m_goal, &tr);
        ++m_out.trials;
        return r.outcome == Outcome::Finished || (r.outcome == Outcome::Converged && r.end == macro::kNever);
    }

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
                const std::pair<int, Tick> key{ch, m_plan.channel(ch)[i].press};
                // Already failed, and nothing it ran over has changed since.
                if (auto it = m_dead.find(key); it != m_dead.end() && !changedSince(it->second.second, it->second.first))
                    continue;
                Plan p = m_plan;
                p.channel(ch).erase(p.channel(ch).begin() + (ptrdiff_t)i);
                Span tried;
                if (tryAdopt(p, &tried)) ++m_out.removed;
                else m_dead[key] = {tried, m_changes.size()};
            }
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
                Plan p = m_plan;
                Holds& q = p.channel(ch);
                q[i - 1].release = q[i].release;
                q.erase(q.begin() + (ptrdiff_t)i);
                if (tryAdopt(p)) ++m_out.merged;
            }
    }

    // A copy with one edge moved, or nothing if the move breaks the order of the holds.
    bool moved(int ch, size_t i, bool press, int64_t t, Plan& out) const {
        out = m_plan;
        Holds& h = out.channel(ch);
        (press ? h[i].press : h[i].release) = (Tick)t;
        if (h[i].press < 1 || h[i].release <= h[i].press) return false;
        if (i && h[i].press <= h[i - 1].release) return false;
        if (i + 1 < h.size() && h[i].release >= h[i + 1].press) return false;
        return humanOk(h, i);
    }

    // Does the plan still finish with this edge moved by `shift`?
    bool fits(int ch, size_t i, bool press, int shift) {
        const Hold& hold = m_plan.channel(ch)[i];
        Plan p;
        return moved(ch, i, press, (int64_t)(press ? hold.press : hold.release) + shift, p) && finishes(p);
    }

    // How far the edge moves each way and still finishes (up to cap).
    int reach(int ch, size_t i, bool press, int dir, int cap) {
        int ok = 0;
        while (ok < cap && fits(ch, i, press, dir * (ok + 1))) ++ok;
        return ok;
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
                    // Three ticks of room each way is comfortable: leave it.
                    if (fits(ch, i, press, -3) && fits(ch, i, press, +3)) continue;
                    int early = reach(ch, i, press, -1, m_o.maxShift), late = reach(ch, i, press, +1, m_o.maxShift);
                    lower(before, early + late + 1);
                    int d = (late - early) / 2;
                    if (d != 0) {
                        const Hold& h = m_plan.channel(ch)[i];
                        Plan p;
                        if (moved(ch, i, press, (int64_t)(press ? h.press : h.release) + d, p) && tryAdopt(p)) {
                            ++m_out.centered;
                            early += d;
                            late -= d;
                        }
                    }
                    lower(after, early + late + 1);
                }
        m_out.narrowestBefore = before;
        m_out.narrowestAfter = after;
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
    KnownStates m_known;
    std::vector<uint64_t> m_rec;
    std::vector<Span> m_changes;   // adopted, in order
    std::map<std::pair<int, Tick>, std::pair<Span, size_t>> m_dead;   // failed removals, by press
};

} // namespace

void optimizePlan(Engine& engine, const Plan& start, const OptimizeOptions& options, OptimizeResult& out) {
    out = OptimizeResult{};
    out.plan = start;
    Optimizer(engine, options, out).run(start);
}

} // namespace kpsim::search
