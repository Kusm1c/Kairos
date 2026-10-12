#include "Pathfinder.hpp"
#include "Timeline.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace kpsim::search {
namespace {

constexpr Tick kFinishSlack = 2400;
constexpr Tick kLearn = 240;   // states kept per trial
constexpr Tick kSurvives = macro::kNever - 1;   // known end: alive at the horizon

uint64_t planHash(const Plan& p) {
    uint64_t h = 1469598103934665603ull;
    for (const Holds* c : {&p.holds, &p.holds2}) {
        for (const auto& x : *c) {
            h = (h ^ x.press) * 1099511628211ull;
            h = (h ^ x.release) * 1099511628211ull;
        }
        h = (h ^ 0xff) * 1099511628211ull;
    }
    return h;
}

// Index of the hold containing t, or of the first hold after t.
size_t holdAtOrAfter(const Holds& h, Tick t) {
    size_t i = 0;
    while (i < h.size() && h[i].release <= t) ++i;
    return i;
}

// Fewest clicks first: removals, then small edits, then new presses.
void candidates(const Plan& base, int ch, Tick death, Tick window, Tick minPress, std::vector<Plan>& out) {
    const Tick lo = death > window ? death - window : 1;
    const Holds& H = base.channel(ch);

    for (size_t i = 0; i < H.size(); ++i)
        if (H[i].press >= lo && H[i].press <= death) {
            Plan p = base;
            p.channel(ch).erase(p.channel(ch).begin() + (ptrdiff_t)i);
            out.push_back(std::move(p));
        }

    static const int kShifts[] = {1, 2, 4, 8, 16};
    for (size_t i = 0; i < H.size(); ++i) {
        if (H[i].press > death || (H[i].release != macro::kNever && H[i].release < lo)) continue;
        const int64_t prevRelease = i ? (int64_t)H[i - 1].release : 0;
        const int64_t nextPress = i + 1 < H.size() ? (int64_t)H[i + 1].press : (int64_t)macro::kNever;
        for (int s : kShifts) {
            for (int dir : {-1, +1}) {
                if (H[i].release != macro::kNever) {
                    int64_t r = (int64_t)H[i].release + dir * s;
                    if (r > (int64_t)H[i].press && r < nextPress) {
                        Plan p = base;
                        p.channel(ch)[i].release = (Tick)r;
                        out.push_back(std::move(p));
                    }
                }
                int64_t a = (int64_t)H[i].press + dir * s;
                if (a >= 1 && a > prevRelease && a < (int64_t)H[i].release) {
                    Plan p = base;
                    p.channel(ch)[i].press = (Tick)a;
                    out.push_back(std::move(p));
                }
            }
        }
    }

    // New presses, coarse first: every 4th tick with a few lengths, then the rest.
    const Tick shortest = std::max<Tick>(1, minPress);
    const Tick kCoarseLengths[] = {shortest, 6, 24, 96};
    const Tick kLengths[] = {shortest, 1, 3, 6, 12, 24, 48, 96};
    auto inserts = [&](Tick step, const auto& lengths) {
        for (int64_t a = (int64_t)death - 1; a >= (int64_t)lo; a -= step) {
            size_t j = holdAtOrAfter(H, (Tick)a);
            if (j < H.size() && H[j].press <= (Tick)a) continue;              // inside a hold
            if (j > 0 && H[j - 1].release >= (Tick)a) continue;               // no gap after previous
            const Tick nextPress = j < H.size() ? H[j].press : macro::kNever;
            Tick lastLen = 0;
            for (Tick len : lengths) {
                Tick release = (Tick)a + len;
                if (release >= nextPress) release = nextPress - 1;
                if (release <= (Tick)a || release - (Tick)a == lastLen) continue;
                lastLen = release - (Tick)a;
                Plan p = base;
                p.channel(ch).insert(p.channel(ch).begin() + (ptrdiff_t)j, Hold{(Tick)a, release});
                out.push_back(std::move(p));
            }
        }
    };
    const Tick fine = std::max<Tick>(1, window / 60);
    inserts(std::max<Tick>(4, fine), kCoarseLengths);
    inserts(fine, kLengths);
}

class Search {
public:
    Search(Engine& e, const PathOptions& o, PathResult& out, bool snapshots)
        : m_e(e), m_o(o), m_out(out), m_tl(e, o.spacing) {
        m_tl.setEnabled(snapshots);
    }

    void run();

private:
    // States of `plan`'s trace from `from`: their future is known.
    void remember(Tick from, Tick to, Tick end) {
        const auto& tr = m_tl.trace();
        to = std::min<Tick>(to, (Tick)tr.size());
        for (Tick t = std::max<Tick>(from, 1); t <= to; ++t)
            if (tr[t - 1]) m_known[stateKey(t, tr[t - 1])] = end;
    }

    // Runs `cand` (a variant of `base`) from the closest snapshot.
    RunResult trial(const Plan& base, const Plan& cand, Tick until) {
        const Tick first = firstDifference(base, cand);
        const Tick last = lastDifference(base, cand);
        m_tl.seek(first >= 2 ? first - 2 : 0);
        const Tick from = m_e.tick();
        Trace tr{&m_rec, &m_known, last};
        RunResult r = m_e.run(cand, until, &tr);
        ++m_out.trials;
        if (r.outcome == Outcome::Converged) {
            ++m_out.pruned;
            if (r.end == macro::kNever) return {Outcome::Finished, until};
            if (r.end == kSurvives) return {Outcome::Reached, until};
            return {Outcome::Died, r.end};
        }
        // States after the edit end the same way for any run reaching them.
        if (m_known.size() < m_o.knownCap) {
            Tick end = r.outcome == Outcome::Died ? r.tick : r.outcome == Outcome::Finished ? macro::kNever : kSurvives;
            Tick a = std::max<Tick>({last, from, 1});
            Tick b = std::min<Tick>({r.tick - 1, a + kLearn, (Tick)m_rec.size()});
            for (Tick t = a; t <= b; ++t)
                if (m_rec[t - 1]) m_known.emplace(stateKey(t, m_rec[t - 1]), end);
        }
        return r;
    }

    // Each new edge of `cand` still gets as far when a human is `margin` ticks off.
    bool robust(const Plan& base, const Plan& cand, int ch, Tick death, const RunResult& got) {
        const Tick m = m_o.rules.margin;
        if (!m) return true;
        const Tick need = got.outcome == Outcome::Died ? std::min<Tick>(got.tick, death + m_o.progress) : death + m_o.progress;
        const Holds& B = cand.channel(ch);
        const Holds& old = base.channel(ch);
        for (size_t i = 0; i < B.size(); ++i)
            for (int edge = 0; edge < 2; ++edge) {
                const Tick t = edge ? B[i].release : B[i].press;
                if (t == macro::kNever) continue;
                if (std::any_of(old.begin(), old.end(), [&](const Hold& h) { return (edge ? h.release : h.press) == t; }))
                    continue;
                for (int d = -(int)m; d <= (int)m; ++d) {
                    if (!d) continue;
                    Plan v = cand;
                    Holds& h = v.channel(ch);
                    (edge ? h[i].release : h[i].press) = (Tick)((int64_t)t + d);
                    bool fits = h[i].press >= 1 && h[i].release > h[i].press && (!i || h[i].press > h[i - 1].release) &&
                                (i + 1 >= h.size() || h[i].release < h[i + 1].press);
                    if (!fits) return false;
                    RunResult r = trial(base, v, death + m_o.horizon);
                    if (r.outcome == Outcome::Died && r.tick < need) return false;
                }
            }
        return true;
    }

    // Replays `plan` from where it differs from `previous`.
    RunResult rebase(const Plan& previous, const Plan& plan, Tick until) {
        Tick c = firstDifference(previous, plan);
        Tick from = c == macro::kNever ? 0 : (c >= 2 ? c - 2 : 0);
        m_tl.truncate(from);
        m_tl.seek(from);
        return m_tl.advance(plan, until);
    }

    void simplify(Plan& plan, Tick goal);
    bool verify(const Plan& p, Tick until);

    std::string progress(Tick death, Tick window) const {
        return "death @" + std::to_string(death) + "  best @" + std::to_string(m_out.furthest) + "  window " +
               std::to_string(window) + "  trials " + std::to_string(m_out.trials) + " (" +
               std::to_string(m_out.pruned) + " cut)  clicks " + std::to_string(m_out.plan.presses());
    }

    Engine& m_e;
    const PathOptions& m_o;
    PathResult& m_out;
    Timeline m_tl;
    std::unordered_set<uint64_t> m_tried;
    KnownStates m_known;
    std::vector<uint64_t> m_rec;
};

void Search::run() {
    std::vector<Plan> history;   // plans before each accepted edit
    Plan plan = m_out.plan;
    m_tried.insert(planHash(plan));
    m_e.reset();
    RunResult r = m_tl.advance(plan, m_o.limit);
    m_out.furthest = std::max(m_out.furthest, r.tick);
    std::vector<Plan> cands;

    while (true) {
        m_out.plan = plan;
        m_out.last = r;
        if (r.outcome == Outcome::Finished) { m_out.solved = true; break; }
        if (r.outcome != Outcome::Died) break;   // limit reached alive

        const Tick death = r.tick;
        const int dying = m_e.twoPlayer() ? r.player : 0;
        m_known.clear();
        remember(death > m_o.maxWindow + 2 ? death - m_o.maxWindow - 2 : 1, death - 1, death);

        // Once one fix clears the obstacle, a few more are tried and the one going furthest wins.
        bool accepted = false;
        Plan next, best;
        Tick bestTick = death;
        size_t left = SIZE_MAX;
        for (Tick w = m_o.window; w <= m_o.maxWindow && !accepted && left; w *= 2) {
            // The dying player's button first; the other one last.
            for (int pass = 0; pass < (m_e.twoPlayer() && w == m_o.maxWindow ? 2 : 1) && !accepted && left; ++pass) {
                cands.clear();
                const int ch = pass ? 1 - dying : dying;
                candidates(plan, ch, death, w, m_o.rules.minPress, cands);
                for (auto& cand : cands) {
                    if (!left) break;
                    if (!m_o.rules.allows(cand) || !m_tried.insert(planHash(cand)).second) continue;
                    RunResult rr = trial(plan, cand, death + m_o.horizon);
                    if ((m_out.trials & 15) == 0) m_e.status(progress(death, w));
                    const bool survives = rr.outcome != Outcome::Died;
                    if (survives || rr.tick > bestTick) {
                        if (!robust(plan, cand, ch, death, rr)) ++m_out.tooTight;
                        else if (survives) { next = std::move(cand); accepted = true; break; }
                        else { bestTick = rr.tick; best = cand; }
                    }
                    if (left != SIZE_MAX) --left;
                    else if (bestTick >= death + m_o.progress) left = m_o.lookahead;
                }
            }
            if (!accepted && bestTick > death) { next = best; accepted = true; }
        }

        Plan previous = plan;
        if (accepted) {
            history.push_back(plan);
            plan = std::move(next);
        } else {
            if (history.empty() || m_out.backtracks >= m_o.maxBacktracks) break;   // stuck
            plan = history.back();
            history.pop_back();
            ++m_out.backtracks;
        }
        r = rebase(previous, plan, m_o.limit);
        m_out.furthest = std::max(m_out.furthest, r.tick);
    }

    m_out.rejectedSnapshots = m_tl.rejected();
    if (!m_tl.enabled()) m_out.snapshotsTrusted = false;
    if (!m_out.solved) return;

    const Plan solvedPlan = plan;
    const Tick solvedTick = m_out.last.tick;
    if (m_o.simplify) simplify(plan, solvedTick + kFinishSlack);

    m_out.verified = verify(plan, solvedTick + kFinishSlack) ||
                     (!(plan == solvedPlan) && verify(solvedPlan, solvedTick + kFinishSlack));
    if (!m_out.verified) {
        m_e.reset();
        m_out.updateSensitive = m_e.run(plan, solvedTick + kFinishSlack).outcome == Outcome::Finished;
    }
    m_out.solved = m_out.verified;
    m_out.rejectedSnapshots = m_tl.rejected();
}

// Drops every click the level can be finished without, last first.
void Search::simplify(Plan& plan, Tick goal) {
    m_known.clear();
    remember(1, goal, macro::kNever);
    const size_t total = plan.presses();
    size_t done = 0;
    for (int ch = 1; ch >= 0; --ch) {
        for (size_t i = plan.channel(ch).size(); i-- > 0;) {
            m_e.status("simplify " + std::to_string(++done) + "/" + std::to_string(total) + "  removed " +
                       std::to_string(total - plan.presses()));
            Plan p = plan;
            p.channel(ch).erase(p.channel(ch).begin() + (ptrdiff_t)i);
            RunResult rr = trial(plan, p, goal);
            if (rr.outcome != Outcome::Finished) continue;
            Plan previous = plan;
            plan = std::move(p);
            rebase(previous, plan, goal);
            m_known.clear();
            remember(1, goal, macro::kNever);
        }
    }
}

// Final proof: the whole plan from the level start, one step per game update.
bool Search::verify(const Plan& p, Tick until) {
    m_e.status("verifying");
    m_e.reset();
    m_e.setStepsPerUpdate(1);
    RunResult v = m_e.run(p, until);
    m_e.setStepsPerUpdate(0);
    if (v.outcome != Outcome::Finished) return false;
    m_out.plan = p;
    m_out.last = v;
    return true;
}

} // namespace

void findPath(Engine& engine, const PathOptions& o, PathResult& out) {
    const Plan start = out.plan;
    {
        Search s(engine, o, out, true);
        s.run();
    }
    // Found through snapshots but does not replay: search again without them.
    if (!out.verified && !out.updateSensitive && out.last.outcome == Outcome::Finished) {
        out.snapshotsTrusted = false;
        out.plan = start;
        Search s(engine, o, out, false);
        s.run();
    }
}

} // namespace kpsim::search
