#pragma once
#include "StateCapture.hpp"
#include "kpsim/search/Engine.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <unordered_map>

namespace kp {

using kpsim::search::Plan;
using kpsim::search::RunResult;
using kpsim::search::Tick;
using kpsim::search::Trace;

struct EngineStats {
    uint64_t steps = 0, saves = 0, restores = 0, mismatches = 0, updates = 0;
    double restoreMs = 0, saveMs = 0, updateMs = 0;
};

// The real game as a search engine. Only one exists at a time.
class GameEngine final : public kpsim::search::Engine {
public:
    explicit GameEngine(PlayLayer* layer);
    ~GameEngine() override;

    void reset() override;
    Tick tick() const override { return m_tick; }
    bool twoPlayer() const override { return m_twoPlayer; }
    int save() override;
    bool restore(int id) override;
    void drop(int id) override;
    RunResult run(const Plan& plan, Tick until, const Trace* trace = nullptr) override;
    uint64_t stateHash() override;
    float playerX() override;
    void setStepsPerUpdate(Tick n) override { m_stepsPerUpdate = n; }
    void status(const std::string& text) override { m_status = text; }
    void progress(double f) override { m_progress = f; }
    double progressValue() const { return m_progress; }
    void restoreMismatch(Tick snapshot, Tick diverged) override;

    const std::string& statusText() const { return m_status; }
    const EngineStats& stats() const { return m_stats; }
    void cancel() { m_cancel = true; }
    // The level was reset by someone else: results would be wrong.
    void invalidate() { m_invalid = true; }

    static GameEngine* active() { return s_active; }
    PlayLayer* layer() const { return m_layer; }

    // Hook entry points.
    bool frozen() const { return !m_inUpdate; }
    bool injecting() const { return m_injecting; }
    void onStep(GJBaseGameLayer* layer);
    void onDeath(PlayerObject* player);
    void onDeathBlocked();
    void onFinish();
    bool ownReset() const { return m_ownReset; }

private:
    struct Save {
        geode::Ref<CheckpointObject> checkpoint;
        Tick tick = 0;
        PlayerCopy players[2];
        LayerCopy layer;
        int64_t seeds[5] = {};
    };

    void resetLayer();
    void advance(Tick steps);
    void observe();   // the state after m_tick
    void checkCancel();
    PlayerObject* player(int p) const { return p ? m_layer->m_player2 : m_layer->m_player1; }

    static GameEngine* s_active;

    PlayLayer* m_layer;
    bool m_twoPlayer = false, m_flip = false;
    Tick m_tick = 0;
    bool m_held[2] = {};
    kpsim::search::Outcome m_stop = kpsim::search::Outcome::Reached;
    Tick m_stopTick = 0, m_stopEnd = 0;
    int m_deathPlayer = 0;
    const Plan* m_plan = nullptr;
    const Trace* m_trace = nullptr;
    bool m_inUpdate = false, m_injecting = false, m_ownReset = false;
    bool m_cancel = false, m_invalid = false, m_deathBlocked = false;
    Tick m_expected = 0;
    Tick m_seen = 0;   // steps the game ran in our last update
    Tick m_stepsPerUpdate = 0;
    std::unordered_map<int, Save> m_saves;
    int m_nextId = 1;
    std::string m_lastRestoreDiff;
    EngineStats m_stats;
    std::string m_status;
    double m_progress = -1;
};

} // namespace kp
