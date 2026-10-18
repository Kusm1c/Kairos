#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dpcore {
struct LevelStats {
    bool ok = false;
    std::size_t objs = 0, portals = 0, pads = 0, orbs = 0, moving = 0;
    double maxX = 0.0;
    std::string unsupported;
};

struct SolveProgress {
    bool running = false;
    long long from = 0, tick = 0, horizon = 0;
    double x = 0.0;
    std::size_t alive = 0;
};

enum { OutcomeFailed = 0, OutcomePartial = 1, OutcomeSolved = 2, OutcomeCancelled = 3 };

struct SolveOutcome {
    int verdict = OutcomeFailed;
    // OutcomeSolved by outliving --horizon rather than by reaching the end (dp progress.hpp).
    bool horizonCut = false;
    long long deepT = -1;
    double deepX = -1.0;
    long long capHits = -1;
    long long workStates = 0;   // dp's SearchOutcome::workStates: the search's own work
    // OutcomeCancelled only: the layer the search was on when it saw the cancel. -1 otherwise.
    long long cancelT = -1;
    long long resimDead = -1, resimFirst = -1;
    const char* resimWhy = nullptr;   // string literal; dp is linked in here
    int resimUid = -1;
    float resimObjX = 0.f, resimObjY = 0.f;
    unsigned long long resimTrig = 0;
    int resimFrame = -1;              // the frame resimObjX/Y are read in
    long long replayDiedT = -1;   // --replay only: where the model died, -1 = it survived
    long long rejoinT = -1;       // --rejoinuse: where the search joined, -1 = no
    long long rejoinBadT = -1;    // ...and where the joined walk left the old plan's, -1 = never
    const char* rejoinBadWhy = nullptr;
    unsigned long long needTrigMask = 0, needTrigPassed = 0;   // wide: see resimTrig
    std::string seedRotQ;
    std::string rotQOrder;
    // Where a tap-gated coin is lost for good (dp progress.hpp, coinGates).
    std::string coinGates;
    // The coins dp does not call missed when passed (dp progress.hpp, coinNoPrune).
    std::string coinNoPrune;
    int startRotHit = -1, startRotGiven = -1;
    std::string startRotMiss;
    int trigWinTouch = -1;
    long long trigTotal = 0, trigRelevantN = 0, trigKept = 0,
              trigDroppedRelevant = 0;
    long long trigDroppedBehind = 0, trigDroppedAhead = 0;
    double trigMaxKeptX = 0.0;
    unsigned long long trigMapSig = 0;   // over the kept uids IN BIT ORDER
    std::string unsupported;
    std::string ladder;
};

struct SolveCheckpoint {
    unsigned long long call = 0;   // the solve call that published it
    std::size_t index = 0;         // its position in that call's order
    long long t0 = 0;              // the anchor the call searches from
    long long tick = 0;            // the last tick its inputs cover
    // `input=press,level`, through the plan writer's own per-mode latency conversion.
    std::vector<std::pair<long long, int>> edges;
};

// ---- the CLI ---------------------------------------------------------------------------------

// leveldp's whole program: dp::cliMain, the cap ladder around the search.
int cliMain(int argc, char** argv);

LevelStats statsFromCsv(const std::string& csv);
void setFileSigHints(const std::unordered_map<std::string, std::string>& hints);
// argv[0] and the level path are supplied here, as in dpbridge::solveInProcess.
int solveInProcess(const std::string& csv, const std::vector<std::string>& args);
std::vector<double> prepMarks();
SolveProgress progress();
SolveOutcome outcome();
void adoptOutcome(const SolveOutcome& s);
// The level held for the call in flight (a copy).
std::string levelCsv();
void releaseCaches();
// ...and roughly how many bytes they hold allocated (dp cli.hpp crossCallCacheBytes). Only between calls.
std::size_t cacheBytes();
// ...the recordings' part of them (dp cli.hpp crossCallGroupBytes). Only between calls.
std::size_t groupCacheBytes();
// The checkpoint channel has a subscriber.
bool checkEnabled();
// The cap ladder's deepest death so far, and the stop for its attempt in progress.
long long ladderBestT();
void ladderStop();
// dp cli.hpp PlainElsewhere, by its five hooks; all null = none.
struct PlainElsewhereHooks {
    bool (*start)(const std::vector<std::string>& argv) = nullptr;
    int (*take)() = nullptr;
    void (*drop)() = nullptr;
    void (*check)(int rc) = nullptr;
    bool (*ready)(long long bestT) = nullptr;
};
void setPlainElsewhere(const PlainElsewhereHooks& h);
bool readFileBytes(const std::string& path, std::string& out);
void checkSubscribe(bool on);
bool nextCheckpoint(SolveCheckpoint& out);
unsigned long long checkCall();
bool passCheckpoint(unsigned long long call, std::size_t index);
// Raise or clear the cancel of the search in flight.
void cancel(bool on);
long long envKillsTotal();
// A run is open in this copy.
bool runOpen();
bool runBegin(int level, unsigned long long seed, std::string& why);
std::string runVerified(const std::vector<std::pair<long long, int>>& flown, long long deathT,
                        double x, bool cleared, long long divT);
std::string runEnd();
void runNoteSplice(const std::vector<std::pair<long long, int>>& edges, long long t0);
std::string coreVersion();
std::string defaultsProfile();
}  // namespace dpcore
