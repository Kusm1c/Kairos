#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dpcore.hpp"

namespace dpbridge {
// What the parser made of an objrects CSV (dpcore.hpp has the fields).
using dpcore::LevelStats;
// Where a running search has got to (dpcore.hpp).
using dpcore::SolveProgress;
// What the last solveInProcess call concluded, and its verdicts (dpcore.hpp).
using dpcore::SolveOutcome;
using dpcore::OutcomeFailed;
using dpcore::OutcomePartial;
using dpcore::OutcomeSolved;
using dpcore::OutcomeCancelled;
// A lineage to fly while the search is still running (dpcore.hpp).
using dpcore::SolveCheckpoint;

// Parse a CSV held in memory (the same bytes that go into objrects.txt).
LevelStats statsFromCsv(const std::string& csv);

int solveInProcess(const std::string& csv, const std::vector<std::string>& args);
void setFileSigHints(const std::unordered_map<std::string, std::string>& hints);

bool releaseCaches();
std::size_t cacheBytes();
// ...the moving-geometry recordings' part of them (dp cli.hpp crossCallGroupBytes); the same rule.
std::size_t groupCacheBytes();

std::string coreVersion();
std::string defaultsProfile();

SolveProgress progress();

// What the last solveInProcess call concluded. Read after solveInProcess returns.
SolveOutcome outcome();
std::vector<double> prepMarks();

void checkSubscribe(bool on);
bool nextCheckpoint(SolveCheckpoint& out);
// Just the owning call's id, with no lock: cheap enough to ask on every physics tick.
unsigned long long checkCall();
bool passCheckpoint(unsigned long long call, std::size_t index);

void cancelSearch(bool on);
long long envKillsTotal();

void plainBeside(int mode);
// The last comparison's line under plainBeside(2), taken (empty when this call made none).
std::string besideCheckTaken();

bool runBegin(int level, unsigned long long seed, std::string& why);
std::string runVerified(const std::vector<std::pair<long long, int>>& flown, long long deathT,
                        double x, bool cleared, long long divT);
std::string runEnd();
void runNoteSplice(const std::vector<std::pair<long long, int>>& edges, long long t0);

// The second copy of the core (dp_bridge2.cpp). Nothing but plainBeside's hooks calls it.
namespace second {
int solve(const std::string& csv, const std::vector<std::string>& argv);   // argv[0] included
void setFileSigHints(const std::unordered_map<std::string, std::string>& hints);
SolveOutcome outcome();
std::string takeOutput();
void cancel(bool on);
long long envKillsTotal();
void releaseCaches();
std::size_t cacheBytes();
std::size_t groupCacheBytes();
bool runBegin(int level, unsigned long long seed, std::string& why);
std::string runVerified(const std::vector<std::pair<long long, int>>& flown, long long deathT,
                        double x, bool cleared, long long divT);
std::string runEnd();
void runNoteSplice(const std::vector<std::pair<long long, int>>& edges, long long t0);
}  // namespace second
}  // namespace dpbridge
