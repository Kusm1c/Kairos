#include "dp/cli.hpp"

#include <cstdio>
#include <iterator>
#include <sstream>

#include "dpcore.hpp"

namespace dpcore {
int cliMain(int argc, char** argv) { return dp::cliMain(argc, argv); }

LevelStats statsFromCsv(const std::string& csv) {
    std::istringstream in(csv);
    dp::Level L = dp::loadLevelFrom(in);
    LevelStats s;
    s.ok = true;
    s.objs = L.objs.size();
    s.portals = L.portals.size();
    s.pads = L.pads.size();
    s.orbs = L.orbs.size();
    s.moving = L.dyn.size();
    s.maxX = L.maxX;
    s.unsupported = L.unsupported;
    return s;
}

void setFileSigHints(const std::unordered_map<std::string, std::string>& hints) {
    dp::g_fileSigHint = hints;
}

int solveInProcess(const std::string& csv, const std::vector<std::string>& args) {
    std::vector<std::string> argv{"leveldp", "(in-process level)"};
    argv.insert(argv.end(), args.begin(), args.end());
    std::vector<char*> ptr;
    ptr.reserve(argv.size());
    for (std::string& a : argv) ptr.push_back(a.data());

    dp::g_levelCsv = csv;
    int rc = -1;
    try {
        rc = dp::cliMain((int)ptr.size(), ptr.data());
    } catch (...) {
        rc = -2;   // never let an exception cross back into the game's frame
    }
    dp::g_levelCsv.clear();
    if (rc >= 0 && dp::g_outcome.planWritten)
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == "--out") {
                dp::runNotePlanFile(args[i + 1]);
                break;
            }
    return rc;
}

std::vector<double> prepMarks() {
    return std::vector<double>(std::begin(dp::g_prepMark), std::end(dp::g_prepMark));
}

SolveProgress progress() {
    SolveProgress p;
    p.running = dp::g_progress.running.load(std::memory_order_acquire);
    p.from = dp::g_progress.from.load(std::memory_order_relaxed);
    p.tick = dp::g_progress.tick.load(std::memory_order_relaxed);
    p.horizon = dp::g_progress.horizon.load(std::memory_order_relaxed);
    p.x = dp::g_progress.x.load(std::memory_order_relaxed);
    p.alive = dp::g_progress.alive.load(std::memory_order_relaxed);
    return p;
}

SolveOutcome outcome() {
#include "dpcore_outcome.inl"
}

void adoptOutcome(const SolveOutcome& s) {
    dp::SearchOutcome& o = dp::g_outcome;
    o.verdict = s.verdict;
    o.horizonCut = s.horizonCut;
    o.deepT = s.deepT;
    o.deepX = s.deepX;
    o.capHits = s.capHits;
    o.workStates = s.workStates;
    o.cancelT = s.cancelT;
    o.resimDead = s.resimDead;
    o.resimFirst = s.resimFirst;
    o.resimWhy = s.resimWhy;
    o.resimUid = s.resimUid;
    o.resimObjX = s.resimObjX;
    o.resimObjY = s.resimObjY;
    o.resimTrig = dp::TouchMask{};
    o.resimTrig.w[0] = s.resimTrig;
    o.resimFrame = s.resimFrame;
    o.replayDiedT = s.replayDiedT;
    o.rejoinT = s.rejoinT;
    o.rejoinBadT = s.rejoinBadT;
    o.rejoinBadWhy = s.rejoinBadWhy;
    o.needTrigMask = dp::TouchMask{};
    o.needTrigMask.w[0] = s.needTrigMask;
    o.needTrigPassed = dp::TouchMask{};
    o.needTrigPassed.w[0] = s.needTrigPassed;
    o.seedRotQ = s.seedRotQ;
    o.rotQOrder = s.rotQOrder;
    o.coinGates = s.coinGates;
    o.coinNoPrune = s.coinNoPrune;
    o.startRotHit = s.startRotHit;
    o.startRotGiven = s.startRotGiven;
    o.startRotMiss = s.startRotMiss;
    o.trigWinTouch = s.trigWinTouch;
    o.trigTotal = s.trigTotal;
    o.trigRelevantN = s.trigRelevantN;
    o.trigKept = s.trigKept;
    o.trigDroppedRelevant = s.trigDroppedRelevant;
    o.trigDroppedBehind = s.trigDroppedBehind;
    o.trigDroppedAhead = s.trigDroppedAhead;
    o.trigMaxKeptX = s.trigMaxKeptX;
    o.trigMapSig = s.trigMapSig;
    o.unsupported = s.unsupported;
}

std::string levelCsv() { return dp::g_levelCsv; }

void releaseCaches() { dp::releaseCrossCallCaches(); }
std::size_t cacheBytes() { return dp::crossCallCacheBytes(); }
std::size_t groupCacheBytes() { return dp::crossCallGroupBytes(); }

bool checkEnabled() { return dp::g_check.enabled.load(std::memory_order_acquire); }

long long ladderBestT() { return dp::g_ladderBestT.load(); }

void ladderStop() { dp::g_ladderStop.store(true); }

void setPlainElsewhere(const PlainElsewhereHooks& h) {
    dp::g_plainElsewhere = dp::PlainElsewhere{h.start, h.take, h.drop, h.check, h.ready};
}

bool readFileBytes(const std::string& path, std::string& out) {
    return dp::readFileBytes(path, out);
}

void checkSubscribe(bool on) {
    dp::g_check.enabled.store(on, std::memory_order_release);
    if (!on) dp::g_check.reset();
}

bool nextCheckpoint(SolveCheckpoint& out) {
    std::lock_guard<std::mutex> g(dp::g_check.m);
    const size_t idx = dp::g_check.judged.load(std::memory_order_acquire);
    if (idx >= dp::g_check.points.size()) return false;
    const dp::SearchCheckpoints::Point& p = dp::g_check.points[idx];
    out.call = (unsigned long long)dp::g_check.call.load(std::memory_order_acquire);
    out.index = idx;
    out.t0 = p.t0;
    out.tick = p.tick;
    out.edges = p.edges;
    return true;
}

unsigned long long checkCall() {
    return (unsigned long long)dp::g_check.call.load(std::memory_order_acquire);
}

bool passCheckpoint(unsigned long long call, std::size_t index) {
    return dp::g_check.pass((uint64_t)call, index);
}

void cancel(bool on) { dp::g_check.cancel.store(on, std::memory_order_release); }

long long envKillsTotal() { return dp::g_envKills.load(std::memory_order_relaxed); }

bool runOpen() { return dp::g_run.open; }

bool runBegin(int level, unsigned long long seed, std::string& why) {
    return dp::runBegin(level, seed, why);
}
std::string runVerified(const std::vector<std::pair<long long, int>>& flown, long long deathT,
                        double x, bool cleared, long long divT) {
    return dp::runVerified(flown, deathT, x, cleared, divT);
}
std::string runEnd() { return dp::runEnd(); }
void runNoteSplice(const std::vector<std::pair<long long, int>>& edges, long long t0) {
    dp::runNoteSplice(edges, t0);
}

std::string coreVersion() {
    return std::string("dp core built ") + __DATE__ + " " + __TIME__;
}

std::string defaultsProfile() {
    dp::resetInvocationState();
    const std::string list = dp::defaultsProfile();
    uint64_t h = 1469598103934665603ULL;   // FNV-1a of the list, so two runs compare by one field
    for (char c : list) {
        h ^= (uint8_t)c;
        h *= 1099511628211ULL;
    }
    std::string core = coreVersion();
    for (char& c : core) if (c == ' ') c = '_';
    char fnv[24];
    std::snprintf(fnv, sizeof(fnv), "%016llx", (unsigned long long)h);
    return "core=" + core + " fnv=" + fnv + " " + list;
}
}  // namespace dpcore
