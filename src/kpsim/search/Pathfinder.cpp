#include "Pathfinder.hpp"
#include "Timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <map>
#include <memory>
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

bool allowedAround(const InputRules& r, const Plan& base, const Plan& cand) {
    const Tick lo = firstDifference(base, cand), hi = lastDifference(base, cand);
    if (lo == macro::kNever) return true;
    for (const Holds* c : {&cand.holds, &cand.holds2}) {
        const Holds& h = *c;
        for (size_t i = 0; i < h.size(); ++i) {
            const Tick end = h[i].release == macro::kNever ? h[i].press : h[i].release;
            if (end < lo || h[i].press > hi) continue;
            if (h[i].release != macro::kNever && h[i].release - h[i].press < r.minPress) return false;
            if (i && (h[i].press - h[i - 1].release < r.minRelease || h[i].press - h[i - 1].press < r.minInterval))
                return false;
            if (i + 1 < h.size() &&
                (h[i + 1].press - h[i].release < r.minRelease || h[i + 1].press - h[i].press < r.minInterval))
                return false;
        }
    }
    return true;
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
        if (m_o.rejoin && m_o.rejoinBase && lastDifference(*m_o.rejoinBase, cand) <= last) tr.nearKnown = m_o.rejoin;
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
                    if (!allowedAround(m_o.rules, plan, cand) || !m_tried.insert(planHash(cand)).second) continue;
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

// ---- a route drawn by hand (followGuide) ----

namespace {
// The drawn line as y(x): points kept left to right, straight between them.
struct Line {
    std::vector<std::pair<float, float>> p;
    bool usable() const { return p.size() >= 2 && p.back().first - p.front().first >= 30.f; }
    float y(float x) const {
        if (x <= p.front().first) return p.front().second;
        if (x >= p.back().first) return p.back().second;
        size_t i = 1;
        while (p[i].first < x) ++i;
        const auto [ax, ay] = p[i - 1];
        const auto [bx, by] = p[i];
        return bx > ax ? ay + (by - ay) * (x - ax) / (bx - ax) : by;
    }
};

// Within `half` of one of the polyline's segments.
bool byPolyline(const std::vector<std::pair<float, float>>& w, float x, float y, float half) {
    for (size_t i = 0; i < w.size(); ++i) {
        const auto [ax, ay] = w[i];
        const auto [bx, by] = i + 1 < w.size() ? w[i + 1] : w[i];
        const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
        const float u = std::clamp(len2 > 0.f ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0.f, 0.f, 1.f);
        const float ex = ax + u * dx - x, ey = ay + u * dy - y;
        if (ex * ex + ey * ey <= half * half) return true;
    }
    return false;
}

template <class... A> std::string fmt(const char* f, A... a) {
    char b[512];
    std::snprintf(b, sizeof(b), f, a...);
    return b;
}

class Tree {
public:
    // `prefix`: the inputs before the start (the base plan's, or a run played by hand).
    Tree(Engine& e, const Plan& base, const GuideOptions& o, const KnownStates& rejoin, Tick end, Tick goal,
         const Plan* prefix = nullptr)
        : m_e(e), m_base(base), m_prefix(prefix ? *prefix : base), m_o(o), m_rejoin(rejoin), m_end(end), m_goal(goal) {}

    // The plan found, verified from the start; false if none.
    bool run(Tick start, Plan& out) {
        const int ch = m_o.player;
        // The snapshot to start from: the last tick before `start` with the buttons up.
        Tick a = start > 1 ? start - 1 : 0;
        while (a > 0 && m_prefix.anyHeldAt(a)) --a;
        m_e.reset();
        if (a && m_e.run(m_prefix, a).outcome != Outcome::Reached) return false;
        const int id = m_e.save();
        if (id < 0) return false;
        Node n;
        n.anchor = std::make_shared<Anchor>(&m_e, id, a);
        n.plan = m_prefix;
        Holds& q = n.plan.channel(ch);
        while (!q.empty() && q.back().press >= start) q.pop_back();
        for (const Hold& h : q) {
            n.lastPress = h.press;
            n.lastEdge = h.release <= start ? h.release : h.press;
        }
        n.held = !q.empty() && q.back().release > start;
        if (n.held) q.back().release = macro::kNever;
        n.tick = start;
        m_open.emplace(n.tick, std::move(n));
        while (!m_open.empty() && expanded < m_o.nodes) {
            auto it = m_open.begin();
            Node cur = std::move(it->second);
            m_open.erase(it);
            if (cur.tick > m_end + m_o.horizon) break;
            ++expanded;
            if ((expanded & 15) == 0) {
                m_e.status("every route by the drawing: " + std::to_string(expanded) + " played, tick " +
                           std::to_string(cur.tick));
                m_e.progress((double)expanded / (double)m_o.nodes);
            }
            if (expand(cur, out)) return true;
        }
        return false;
    }

    size_t expanded = 0, merged = 0, deaths = 0, rejoins = 0;
    Tick furthest = 0;

private:
    struct Anchor {
        Anchor(Engine* e, int id, Tick tick) : e(e), id(id), tick(tick) {}
        Anchor(const Anchor&) = delete;
        ~Anchor() { e->drop(id); }
        Engine* e;
        int id;
        Tick tick;
    };
    struct Node {
        Plan plan;   // up to its last switch, the button held on if held
        Tick tick = 0, lastEdge = 0, lastPress = 0;
        bool held = false;
        std::shared_ptr<Anchor> anchor;
    };

    bool expand(Node& n, Plan& out) {
        const int ch = m_o.player;
        const auto& r = m_o.rules;
        if (!m_e.restore(n.anchor->id)) return false;
        if (n.tick > n.anchor->tick && m_e.run(n.plan, n.tick).outcome != Outcome::Reached) return false;
        // Button up: a snapshot here for its children.
        if (!n.held && n.anchor->tick < n.tick) {
            const int id = m_e.save();
            if (id >= 0) n.anchor = std::make_shared<Anchor>(&m_e, id, n.tick);
        }
        std::vector<uint64_t> rec;
        Trace tr;
        tr.nearRecord = &rec;
        tr.nearKnown = &m_rejoin;
        tr.knownFrom = m_end;
        const Tick until = n.tick + m_o.longest;
        const RunResult res = m_e.run(n.plan, until, &tr);
        furthest = std::max(furthest, res.tick);
        if (res.outcome == Outcome::Converged || res.outcome == Outcome::Finished) {
            ++rejoins;
            Plan p;
            if ((res.outcome == Outcome::Finished ? (p = n.plan, true) : splice(n, res.tick, p)) && verify(p)) {
                out = std::move(p);
                return true;
            }
        }
        if (res.outcome == Outcome::Died) ++deaths;
        const Tick alive = res.outcome == Outcome::Reached ? until : res.tick - 1;   // last tick it lived through
        const Tick first = std::max<Tick>(n.tick + 1, n.lastEdge + (n.held ? r.minPress : r.minRelease));
        for (Tick t = first; t <= alive + 1 && t <= until; ++t) {
            if (!n.held && n.lastPress && t - n.lastPress < r.minInterval) continue;   // a press: the click rate
            if (t < 2 || t - 1 > rec.size() || !rec[t - 2]) continue;
            const uint64_t at = rec[t - 2];   // the state it switches from
            const Tick sincePress = n.held ? std::min<Tick>(t - n.lastPress, r.minInterval) : 0;   // the child's
            const uint64_t key = stateKey(t, at ^ (n.held ? 0x5bd1e995ull : 0) ^ ((uint64_t)sincePress << 48));
            if (!m_seen.insert(key).second) {
                ++merged;
                continue;
            }
            Node c;
            c.plan = n.plan;
            Holds& q = c.plan.channel(ch);
            if (n.held) q.back().release = t;
            else q.push_back({t, macro::kNever});
            c.tick = t;
            c.lastEdge = t;
            c.lastPress = n.held ? n.lastPress : t;
            c.held = !n.held;
            c.anchor = n.anchor;
            m_open.emplace(t, std::move(c));
        }
        return false;
    }

    // The route up to tc, then the base plan's inputs: the same state as the base run at tc.
    bool splice(const Node& n, Tick tc, Plan& out) const {
        const int ch = m_o.player;
        if (n.held != m_base.heldAt(tc, ch)) return false;
        out = n.plan;
        Holds& q = out.channel(ch);
        const Holds& b = m_base.channel(ch);
        size_t i = 0;
        while (i < b.size() && b[i].press <= tc) ++i;
        if (n.held) q.back().release = b[i - 1].release;
        q.insert(q.end(), b.begin() + (ptrdiff_t)i, b.end());
        return out.valid();
    }

    bool verify(const Plan& p) {
        m_e.status("verifying the route from the start");
        m_e.reset();
        m_e.setStepsPerUpdate(1);
        const RunResult v = m_e.run(p, m_goal);
        m_e.setStepsPerUpdate(0);
        return v.outcome == Outcome::Finished;
    }

    Engine& m_e;
    const Plan& m_base;
    const Plan& m_prefix;
    const GuideOptions& m_o;
    const KnownStates& m_rejoin;
    Tick m_end, m_goal;
    std::multimap<Tick, Node> m_open;   // by tick: every route, earliest first
    std::unordered_set<uint64_t> m_seen;
};

Tick learnRejoin(Engine& engine, const Plan& base, Tick from, KnownStates& rejoin) {
    std::vector<uint64_t> nearRec;
    engine.reset();
    Trace tr;
    tr.nearRecord = &nearRec;
    const RunResult r = engine.run(base, 240u * 60u * 30u, &tr);
    if (r.outcome != Outcome::Finished) return 0;
    for (Tick t = std::max<Tick>(from, 1); t <= nearRec.size(); ++t)
        if (nearRec[t - 1]) rejoin[stateKey(t, nearRec[t - 1])] = macro::kNever;
    return r.tick;
}

void joinPlayed(Engine& engine, const Plan& base, const GuideOptions& o, GuideResult& out) {
    const int ch = o.player;
    const auto& r = o.rules;
    const Tick until = o.playedUntil;
    std::vector<Tick> e, b;
    for (const Hold& h : o.played->channel(ch))
        for (Tick t : {h.press, h.release})
            if (t <= until) e.push_back(t);
    for (const Hold& h : base.channel(ch))
        for (Tick t : {h.press, h.release})
            if (t != macro::kNever) b.push_back(t);
    const bool held = e.size() % 2 == 1;
    Tick from = until + 1;
    if (!e.empty()) from = std::max<Tick>(from, e.back() + (held ? r.minPress : r.minRelease));
    size_t k = std::lower_bound(b.begin(), b.end(), from) - b.begin();
    while (k < b.size() && (k % 2 == 1) != held) ++k;   // the base leaving the played run's level
    e.insert(e.end(), b.begin() + (ptrdiff_t)std::min(k, b.size()), b.end());
    Plan start = *o.played;
    Holds& q = start.channel(ch);
    q.clear();
    for (size_t i = 0; i < e.size(); i += 2) q.push_back({e[i], i + 1 < e.size() ? e[i + 1] : macro::kNever});

    engine.status("learning the base run");
    KnownStates rejoin;
    const Tick finish = learnRejoin(engine, base, until, rejoin);
    if (!finish) {
        out.error = "The base plan does not finish the level.";
        return;
    }
    PathOptions po;
    po.rules = r;
    po.simplify = false;
    po.rejoin = &rejoin;
    po.rejoinBase = &base;
    PathResult pr;
    pr.plan = start;
    findPath(engine, po, pr);
    out.trials = pr.trials;
    out.log = fmt("played by hand to tick %u; repair: furthest %u, %llu trials, %zu backtracks", until, pr.furthest,
                  (unsigned long long)pr.trials, pr.backtracks);
    Plan found;
    bool ok = pr.verified;
    if (ok) found = pr.plan;
    if (!ok) {
        Tree tree(engine, base, o, rejoin, until, finish + 2400, &start);
        ok = tree.run(until, found);
        out.log += fmt("; every route: %zu played, %zu merged, %zu deaths, %zu back on the run, furthest %u",
                       tree.expanded, tree.merged, tree.deaths, tree.rejoins, tree.furthest);
        if (!ok)
            out.error = fmt("Your run could not be joined back to the macro (furthest: %.2f s).",
                            std::max(pr.furthest, tree.furthest) / 240.0);
    }
    out.found = out.verified = ok;
    if (ok) {
        out.plan = found;
        out.from = firstDifference(base, found);
        out.to = lastDifference(base, found);
    }
}
} // namespace

void followGuide(Engine& engine, const Plan& base, const GuideOptions& o, GuideResult& out) {
    out = GuideResult{};
    out.plan = base;
    if (o.played) return joinPlayed(engine, base, o, out);
    Line line;
    for (const auto& pt : o.points)
        if (line.p.empty() || pt.first > line.p.back().first) line.p.push_back(pt);
    const bool hasLine = line.usable();
    float x0 = 1e30f, x1 = -1e30f;   // the drawing's extent
    if (hasLine) {
        x0 = line.p.front().first;
        x1 = line.p.back().first;
    }
    for (const auto& w : o.walls)
        for (const auto& [x, y] : w) {
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
        }
    if (x1 - x0 < 10.f) {
        out.error = "Nothing drawn: draw a line left to right, or a wall.";
        return;
    }

    // The base run's states past the drawing, on the near grid: a fix back on one finishes as it does.
    engine.status("learning the base run");
    engine.reset();
    const Tick fast = o.drawnAt > 720 ? o.drawnAt - 720 : 0;
    Tick start = 0, end = 0;   // the base run's ticks just before the drawing and past it
    if (fast) engine.run(base, fast);
    for (Tick t = fast + 1; !end; ++t) {
        if (engine.run(base, t).outcome != Outcome::Reached) break;
        if (!start && engine.playerX() >= x0 - 30.f) start = t;
        if (engine.playerX() >= x1) end = t;
        if (t > o.drawnAt + 1440) break;
    }
    if (!end) {
        out.error = "The drawing is not on the player's route there.";
        return;
    }
    std::vector<uint64_t> nearRec;
    engine.reset();
    Trace tr;
    tr.nearRecord = &nearRec;
    const RunResult baseRun = engine.run(base, 240u * 60u * 30u, &tr);
    if (baseRun.outcome != Outcome::Finished) {
        out.error = "The base plan does not finish the level.";
        return;
    }
    KnownStates rejoin;
    for (Tick t = end; t <= nearRec.size(); ++t)
        if (nearRec[t - 1]) rejoin[stateKey(t, nearRec[t - 1])] = macro::kNever;

    // The drawing as hazards: by a wall, or off the line's corridor over its extent.
    const float lx0 = hasLine ? line.p.front().first : 0.f, lx1 = hasLine ? line.p.back().first : 0.f;
    engine.forbiddenPlayer = o.player;
    engine.forbidden = [&](float x, float y) {
        if (x < x0 - o.wallHalf || x > x1 + o.wallHalf) return false;
        for (const auto& w : o.walls)
            if (byPolyline(w, x, y, o.wallHalf)) return true;
        return hasLine && x >= lx0 && x <= lx1 && std::fabs(y - line.y(x)) > o.corridor;
    };
    struct Off {
        Engine& e;
        ~Off() { e.forbidden = nullptr; }
    } off{engine};

    PathOptions po;
    po.rules = o.rules;
    po.simplify = false;
    po.rejoin = &rejoin;
    po.rejoinBase = &base;
    PathResult pr;
    pr.plan = base;
    if (o.repairFirst) findPath(engine, po, pr);
    out.trials = pr.trials;
    out.log = fmt("drawing x %.0f-%.0f (ticks %u-%u), %zu walls, %s; repair: furthest %u, %llu trials (%llu cut), "
                  "%zu backtracks, %llu too tight",
                  x0, x1, start, end, o.walls.size(), hasLine ? "a line" : "no line", pr.furthest,
                  (unsigned long long)pr.trials, (unsigned long long)pr.pruned, pr.backtracks,
                  (unsigned long long)pr.tooTight);
    Plan found;
    bool ok = pr.verified;
    if (ok) found = pr.plan;
    // Small fixes were not enough (a portal turning the gravity, say): every route by the drawing.
    if (!ok && start) {
        Tree tree(engine, base, o, rejoin, end, baseRun.tick + 2400);
        ok = tree.run(start, found);
        out.log += fmt("; every route: %zu played, %zu merged, %zu deaths, %zu back on the run, furthest %u",
                       tree.expanded, tree.merged, tree.deaths, tree.rejoins, tree.furthest);
        if (!ok)
            out.error = fmt("No route keeps off the drawing (furthest: %.2f s).",
                            std::max(pr.furthest, tree.furthest) / 240.0);
    }
    out.found = out.verified = ok;
    if (ok) {
        out.plan = found;
        out.from = firstDifference(base, found);
        out.to = lastDifference(base, found);
    }
}
} // namespace kpsim::search
