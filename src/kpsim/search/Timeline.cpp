#include "Timeline.hpp"

#include <algorithm>

namespace kpsim::search {

RunResult Timeline::advance(const Plan& plan, Tick until, std::vector<uint64_t>* nearTrace) {
    m_plan = plan;
    if (m_trace.size() > m_engine.tick()) m_trace.resize(m_engine.tick());
    Trace rec{&m_trace};
    rec.nearRecord = nearTrace;
    for (;;) {
        Tick now = m_engine.tick();
        Tick next = (now / m_spacing + 1) * m_spacing;
        Tick stop = std::min(next, until);
        RunResult r = m_engine.run(plan, stop, &rec);
        if (r.outcome != Outcome::Reached || stop == until) return r;
        // Buttons up on both sides: a restore needs no input edge.
        if (m_enabled && !m_bad.count(stop) && !plan.anyHeldAt(stop) && !plan.anyHeldAt(stop + 1) &&
            (m_snaps.empty() || m_snaps.back().tick < stop)) {
            int id = m_engine.save();
            if (id >= 0) m_snaps.push_back({stop, id, false});
        }
    }
}

// Replays the plan from the snapshot and compares with the recorded trace.
Tick Timeline::check(const Snap& s) {
    Tick end = std::min<Tick>(s.tick + m_spacing, (Tick)m_trace.size());
    if (end <= s.tick) return 0;
    if (!m_engine.restore(s.id)) return s.tick;
    Trace expect{nullptr, nullptr, 0, &m_trace};
    RunResult r = m_engine.run(m_plan, end, &expect);
    return r.outcome == Outcome::Reached && r.tick == end ? 0 : std::max<Tick>(r.tick, s.tick);
}

Tick Timeline::seek(Tick t) {
    while (m_enabled && !m_snaps.empty()) {
        auto it = std::upper_bound(m_snaps.begin(), m_snaps.end(), t,
                                   [](Tick v, const Snap& s) { return v < s.tick; });
        if (it == m_snaps.begin()) break;
        --it;
        Tick diverged = it->checked ? 0 : check(*it);
        bool ok = diverged == 0;
        if (!ok) {
            m_engine.restoreMismatch(it->tick, diverged);
            // Mostly failing: this level's snapshots are not trustworthy.
            if (m_bad.insert(it->tick).second && ++m_rejected >= 4 && m_rejected > m_accepted) setEnabled(false);
        } else if (!it->checked) {
            it->checked = true;
            ++m_accepted;
        }
        if (ok && m_engine.restore(it->id)) return it->tick;
        if (!m_enabled) break;
        m_engine.drop(it->id);
        m_snaps.erase(it);
    }
    m_engine.reset();
    return 0;
}

void Timeline::truncate(Tick t) {
    while (!m_snaps.empty() && m_snaps.back().tick > t) {
        m_engine.drop(m_snaps.back().id);
        m_snaps.pop_back();
    }
    if (m_trace.size() > t) m_trace.resize(t);
}

void Timeline::splice(const Plan& plan, Tick from, Tick to, const std::vector<uint64_t>& rec) {
    m_plan = plan;
    for (size_t i = m_snaps.size(); i-- > 0;)
        if (m_snaps[i].tick > from && m_snaps[i].tick < to) {
            m_engine.drop(m_snaps[i].id);
            m_snaps.erase(m_snaps.begin() + (ptrdiff_t)i);
        }
    for (Tick t = std::max<Tick>(from, 1); t < to && t <= rec.size(); ++t) {
        if (m_trace.size() < t) m_trace.resize(t, 0);
        m_trace[t - 1] = rec[t - 1];
    }
}

void Timeline::dropSnaps() {
    for (auto& s : m_snaps) m_engine.drop(s.id);
    m_snaps.clear();
}

void Timeline::clear() {
    dropSnaps();
    m_trace.clear();
}

namespace {

std::vector<Tick> edges(const Holds& h) {
    std::vector<Tick> e;
    for (auto& x : h) { e.push_back(x.press); e.push_back(x.release); }
    return e;
}

Tick firstIn(const Holds& a, const Holds& b) {
    auto ea = edges(a), eb = edges(b);
    size_t i = 0;
    while (i < ea.size() && i < eb.size() && ea[i] == eb[i]) ++i;
    if (i == ea.size() && i == eb.size()) return macro::kNever;
    Tick ta = i < ea.size() ? ea[i] : macro::kNever;
    Tick tb = i < eb.size() ? eb[i] : macro::kNever;
    return std::min(ta, tb);
}

Tick lastIn(const Holds& a, const Holds& b) {
    auto ea = edges(a), eb = edges(b);
    size_t i = ea.size(), j = eb.size();
    while (i > 0 && j > 0 && ea[i - 1] == eb[j - 1]) { --i; --j; }
    if (i == 0 && j == 0) return 0;
    Tick ta = i > 0 ? ea[i - 1] : 0;
    Tick tb = j > 0 ? eb[j - 1] : 0;
    return std::max(ta, tb);
}

} // namespace

Tick firstDifference(const Plan& a, const Plan& b) {
    return std::min(firstIn(a.holds, b.holds), firstIn(a.holds2, b.holds2));
}

Tick lastDifference(const Plan& a, const Plan& b) {
    return std::max(lastIn(a.holds, b.holds), lastIn(a.holds2, b.holds2));
}

} // namespace kpsim::search
