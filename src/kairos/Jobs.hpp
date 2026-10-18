#pragma once
#include "kpsim/macro/Macro.hpp"
#include "kpsim/search/FrameWindows.hpp"
#include "kpsim/search/InputRules.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace kp {
enum class JobKind { FindPath, Windows, Watch, Optimize, Rhythm, Check, Reroute, Guide };

struct JobRequest {
    JobKind kind = JobKind::FindPath;
    kpsim::macro::Plan plan;     // Windows / Watch, per button
    kpsim::macro::Plan merged;   // the same on one button (1-player levels)
    std::string source;        // macro file name
    kpsim::search::WindowReport windows;   // Watch / Rhythm: the macro's frame windows, if measured
    std::string fitLimits;    // Optimize: where the solver dropped the human limits ("" = it did not)
    kpsim::search::EdgeTests check;   // Check: a narrow edge's tries; Reroute: the edge
    bool follow = false;   // Rhythm: the macro plays and the player's clicks are only judged
    bool ofSolution = false;   // Reroute, Guide: the plan is the level's solution (saved if improved)
    std::vector<std::pair<float, float>> guide;   // Guide: the line drawn over the level (world points)
    std::vector<std::vector<std::pair<float, float>>> guideWalls;   // ...and the walls
    kpsim::macro::Plan played;   // Guide: or a run played by hand (Play it slow), kept up to playedUntil
    kpsim::macro::Tick playedUntil = 0;
};

// Opens the level on a narrow edge and replays its tries one by one (setting window-check-tests).
void startCheck(GJGameLevel* level, const kpsim::search::EdgeTests& tests);
// Find better route: the inputs before that narrow edge re-timed for more room (Optimizer's `only`).
void startReroute(GJGameLevel* level, const kpsim::search::EdgeTests& tests);
// A route along a line drawn over the level during Check, off the walls drawn (Pathfinder's followGuide).
void startGuide(GJGameLevel* level, const kpsim::search::EdgeTests& tests, std::vector<std::pair<float, float>> line,
                std::vector<std::vector<std::pair<float, float>>> walls);
// Play it slow: the run played by hand up to `until`, joined back to the macro.
void startPlayed(GJGameLevel* level, const kpsim::search::EdgeTests& tests, kpsim::macro::Plan played,
                 kpsim::macro::Tick until);
// `plan` is the level's solution as loaded for the jobs (Menu.cpp).
bool isSolutionPlan(GJGameLevel* level, const kpsim::macro::Plan& plan);

// The level's last frame windows, if they were measured on this very plan.
kpsim::search::WindowReport windowsFor(GJGameLevel* level, const kpsim::macro::Plan& plan);

// A macro is being replayed by Watch.
bool watchActive();

// The human limits of the settings, or none with "Go crazy!".
kpsim::search::InputRules humanRules();

// Where the plan first needs faster inputs than those limits, and what, in a sentence; "" if never.
std::string limitBreach(const kpsim::macro::Plan& plan);

// The solver's mode menu for this level (src/mod/hooks_playmenu.cpp); false if it cannot open.
bool openSolveMenu(LevelInfoLayer* layer);
bool openSolveMenu(EditLevelLayer* layer);

// A Solve under way (SolverState.cpp), for the Kairos panel.
struct SolveStatus {
    bool solving = false;
    double level = -1;   // how far a replay has got, 0..1 (-1 unknown)
    double search = -1;  // the current search, 0..1 (-1 none)
    int iteration = 0;
    std::string phase;
    double seconds = 0;
};
SolveStatus solveStatus();
// Ends the Solve and leaves the level, as the solver's quit key does.
void stopSolve();

// Opens the level and runs the job once it is loaded.
void startJob(GJGameLevel* level, JobRequest request);

std::filesystem::path solutionPath(GJGameLevel* level);
// Written by the solver (src/mod).
std::filesystem::path solverSolutionPath(GJGameLevel* level);
std::filesystem::path reportPath(GJGameLevel* level);
// The last frame windows of the level, for the stats menu.
std::filesystem::path statsPath(GJGameLevel* level);
std::filesystem::path outputDir();

void saveModelWorld(int levelId, const std::filesystem::path& dir);
// `plan` replayed in that model: the tick it dies at, -1 if it survives, -2 if it cannot run.
long long modelReplay(const std::filesystem::path& dir, const kpsim::macro::Plan& plan, double& seconds,
                      std::string& error);
std::filesystem::path modelDir(GJGameLevel* level);
} // namespace kp
