#pragma once
#include "kpsim/macro/Macro.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace kpsim::search {

using macro::Hold;
using macro::Holds;
using macro::Plan;
using macro::Tick;

// Converged: reached a state whose future is known. Diverged: left the expected trace.
enum class Outcome { Died, Finished, Reached, Converged, Diverged };

// (tick, state hash) -> tick the run from there ends at (kNever: it passes).
using KnownStates = std::unordered_map<uint64_t, Tick>;

inline uint64_t stateKey(Tick t, uint64_t hash) { return hash ^ (uint64_t(t) * 0x9E3779B97F4A7C15ull); }

// Per-tick state hashes; index = tick - 1 (state after that tick).
struct Trace {
    std::vector<uint64_t>* record = nullptr;        // filled while running
    const KnownStates* known = nullptr;             // Converged on a match
    Tick knownFrom = 0;                             // inputs equal the known runs' after this tick
    const std::vector<uint64_t>* expect = nullptr;  // Diverged on a mismatch
    // The same, with the state on a kNearGrid grid: back on a known run, not exactly on it.
    std::vector<uint64_t>* nearRecord = nullptr;
    const KnownStates* nearKnown = nullptr;
    // In flight only (ship, UFO, swing): back on a known run roughly (looseCell), any route.
    std::vector<uint64_t>* looseRecord = nullptr;
    const KnownStates* looseKnown = nullptr;
};

constexpr double kNearGrid = 1.0 / 1024;   // units

// Player modes, as Engine::mode reports them.
enum Mode { kCube, kShip, kBall, kUfo, kWave, kRobot, kSpider, kSwing };
inline bool flyingMode(int m) { return m == kShip || m == kUfo || m == kSwing; }

// "Roughly on the route" in flight: y on a 2-unit grid, vy on a 0.5 grid, a neighbour cell counts.
constexpr double kLooseY = 2.0, kLooseVy = 0.5;
struct LooseCell {
    uint64_t salt = 0;   // mode, gravity
    int64_t y = 0, vy = 0;
};
inline LooseCell looseCell(int mode, bool upsideDown, double y, double vy) {
    return {(uint64_t)(mode * 2 + (upsideDown ? 1 : 0)) + 1, (int64_t)std::llround(y / kLooseY),
            (int64_t)std::llround(vy / kLooseVy)};
}
inline uint64_t looseKey(const LooseCell& c, int dy = 0, int dvy = 0) {
    const uint64_t h = c.salt * 0x9E3779B97F4A7C15ull ^ (uint64_t)(c.y + dy) * 0xC2B2AE3D27D4EB4Full ^
                       (uint64_t)(c.vy + dvy) * 0x165667B19E3779F9ull;
    return h ? h : 1;
}
// The end tick of a known run in this cell or a neighbour, or false.
inline bool looseFind(const KnownStates& known, Tick t, const LooseCell& c, Tick& end) {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dvy = -1; dvy <= 1; ++dvy)
            if (auto it = known.find(stateKey(t, looseKey(c, dy, dvy))); it != known.end()) {
                end = it->second;
                return true;
            }
    return false;
}

struct RunResult {
    Outcome outcome = Outcome::Reached;
    Tick tick = 0;    // death, finish, convergence, or the requested tick
    Tick end = 0;     // Converged: the known end tick
    int player = 0;   // Died: 1 = player 2
};

// Thrown by an engine when the user cancels.
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("cancelled") {}
};

// Thrown when the engine can no longer be trusted (wrong step count, ...).
struct EngineError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Engine {
public:
    virtual ~Engine() = default;

    // Level start, tick 0, buttons up.
    virtual void reset() = 0;
    virtual Tick tick() const = 0;
    // Two buttons (2-player level).
    virtual bool twoPlayer() const { return false; }

    // Snapshot of the current state; -1 if not possible (button held, dead).
    virtual int save() = 0;
    // false: the snapshot is unusable and was dropped.
    virtual bool restore(int id) = 0;
    virtual void drop(int id) = 0;

    // Steps from tick()+1 until death, level end, a trace event, or `until`.
    virtual RunResult run(const Plan& plan, Tick until, const Trace* trace = nullptr) = 0;

    virtual uint64_t stateHash() = 0;
    virtual float playerX() = 0;
    // 1 on the ground, 0 in the air, -1 unknown.
    virtual int onGround(int player) { return -1; }
    // Mode (kCube...), -1 unknown.
    virtual int mode(int player) { return -1; }

    // Physics steps per game update (1 = like 240 FPS); 0 = engine default.
    virtual void setStepsPerUpdate(Tick) {}

    virtual void status(const std::string& text) = 0;
    // How much of the job is done, 0 to 1 (negative: unknown).
    virtual void progress(double) {}
    // A snapshot whose replay left the recorded trace at `diverged`.
    virtual void restoreMismatch(Tick snapshot, Tick diverged) {}
};

} // namespace kpsim::search
