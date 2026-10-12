#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kpsim::macro {

// Physics step index; the first step after a reset is 1.
using Tick = uint32_t;
constexpr Tick kNever = UINT32_MAX;

// Button held on ticks press <= t < release.
struct Hold {
    Tick press = 0;
    Tick release = 0;
    bool operator==(const Hold&) const = default;
};

using Holds = std::vector<Hold>;   // sorted, gap >= 1 tick between holds

struct Plan {
    Holds holds;    // player 1 button
    Holds holds2;   // player 2 button (2-player levels)

    Holds& channel(int p) { return p ? holds2 : holds; }
    const Holds& channel(int p) const { return p ? holds2 : holds; }
    bool heldAt(Tick t, int p = 0) const;
    bool anyHeldAt(Tick t) const { return heldAt(t, 0) || heldAt(t, 1); }
    Tick lastEdge() const;
    size_t presses() const { return holds.size() + holds2.size(); }
    // Sorts and merges overlapping or touching holds.
    void normalize();
    bool valid() const;
    // Same holds, same fingerprint.
    uint64_t fingerprint() const;
    bool operator==(const Plan&) const = default;
};

struct Macro {
    Plan plan;     // per button
    Plan merged;   // both buttons on player 1 (1-player levels)
    double framerate = 240.0;
    uint32_t levelId = 0;
    std::string levelName;
    std::string bot;
    size_t deaths = 0;
    bool platformer = false;
    bool player2Inputs = false;
};

// .gdr2 (binary) or .gdr (MessagePack). Jump button only.
bool readMacro(const std::vector<uint8_t>& bytes, Macro& out, std::string& err);
bool readMacroFile(const std::filesystem::path& path, Macro& out, std::string& err);
// The solver's plan file (input=<step>,<buttons>, bit 0 = player 1); its last attempt.
bool readSolverPlan(const std::filesystem::path& path, Plan& out, std::string& err);
// The same format, so the solver's own Replay plays the plan.
std::string writeSolverPlan(const Plan& plan);

std::vector<uint8_t> writeGdr2(const Plan& plan, uint32_t levelId, const std::string& levelName);
bool writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes);

} // namespace kpsim::macro
