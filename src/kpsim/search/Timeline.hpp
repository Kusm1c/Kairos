#pragma once
#include "Engine.hpp"

#include <set>
#include <vector>

namespace kpsim::search {
class Timeline {
public:
    Timeline(Engine& engine, Tick spacing) : m_engine(engine), m_spacing(spacing) {}
    ~Timeline() { clear(); }

    RunResult advance(const Plan& plan, Tick until, std::vector<uint64_t>* nearTrace = nullptr,
                      std::vector<uint64_t>* looseTrace = nullptr);
    // Engine at the latest usable snapshot <= t (else level start). Returns its tick.
    Tick seek(Tick t);
    // Plan changed after t: later snapshots are invalid.
    void truncate(Tick t);
    void splice(const Plan& plan, Tick from, Tick to, const std::vector<uint64_t>& rec);
    void clear();
    // An extra snapshot at t on the current plan, until unpinned (0: none taken).
    int pin(Tick t);
    void unpin(int id);
    size_t size() const { return m_snaps.size(); }
    // Off: every seek restarts the level (slow, needs no snapshot).
    void setEnabled(bool on) { m_enabled = on; if (!on) dropSnaps(); }
    bool enabled() const { return m_enabled; }
    // Hash of the state after each tick of the plan (index tick - 1, 0 = unknown).
    const std::vector<uint64_t>& trace() const { return m_trace; }
    size_t rejected() const { return m_rejected; }

private:
    struct Snap { Tick tick; int id; bool checked; };
    Tick check(const Snap& s);   // 0 when it replays the same
    void dropSnaps();

    Engine& m_engine;
    Tick m_spacing;
    bool m_enabled = true;
    std::vector<Snap> m_snaps;   // ascending ticks
    Plan m_plan;
    std::vector<uint64_t> m_trace;
    size_t m_rejected = 0, m_accepted = 0;
    std::set<Tick> m_bad;   // ticks whose snapshot replayed differently
};

// First tick where the two plans differ (kNever if equal).
Tick firstDifference(const Plan& a, const Plan& b);
// Last tick where the two plans differ (0 if equal).
Tick lastDifference(const Plan& a, const Plan& b);
} // namespace kpsim::search
