#pragma once
#include "kpsim/macro/Macro.hpp"
#include "kpsim/search/FrameWindows.hpp"
#include "kpsim/search/InputRules.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>

namespace kp {

enum class JobKind { FindPath, Windows, Watch, Optimize, Rhythm };

struct JobRequest {
    JobKind kind = JobKind::FindPath;
    kpsim::macro::Plan plan;     // Windows / Watch, per button
    kpsim::macro::Plan merged;   // the same on one button (1-player levels)
    std::string source;        // macro file name
    kpsim::search::WindowReport windows;   // Watch / Rhythm: the macro's frame windows, if measured
};

// The level's last frame windows, if they were measured on this very plan.
kpsim::search::WindowReport windowsFor(GJGameLevel* level, const kpsim::macro::Plan& plan);

// A macro is being replayed by Watch.
bool watchActive();

// The human limits of the settings, or none with "Go crazy!".
kpsim::search::InputRules humanRules();

// Where the plan first needs faster inputs than those limits, and what, in a sentence; "" if never.
std::string limitBreach(const kpsim::macro::Plan& plan);

// Opens the level and runs the job once it is loaded.
void startJob(GJGameLevel* level, JobRequest request);

std::filesystem::path solutionPath(GJGameLevel* level);
// Written by the solver (src/mod).
std::filesystem::path solverSolutionPath(GJGameLevel* level);
std::filesystem::path reportPath(GJGameLevel* level);
// The last frame windows of the level, for the stats menu.
std::filesystem::path statsPath(GJGameLevel* level);
std::filesystem::path outputDir();

} // namespace kp
