#include "mod/playlayer_helpers.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJEffectManager.hpp>

using namespace p1;

#ifdef GEODE_IS_WINDOWS

constexpr double kFirstAttemptCount = 0.0;

class $modify(AttemptPinLayer, GJBaseGameLayer) {
    // Type 5 is the attempt count (+0x3084 on the layer); every other type is the game's.
    double getItemValue(int type, int id) {
        const double v = GJBaseGameLayer::getItemValue(type, id);
        if (type != 5 || !botDriving()) return v;
        if (g_attemptReadsSeen++ < 20)
            writeResult("attemptone: attempt count read on attempt " + std::to_string(g_attempt)
                        + ": the game has " + std::to_string((long long)v)
                        + (g_attemptOne ? ", answered as on the first attempt"
                           : g_attemptCount >= 0 ? ", answered as " + std::to_string(g_attemptCount)
                                                     + " (cfg attemptcount)"
                                                 : std::string(", left as it is")));
        if (g_attemptOne) {
            ++g_attemptReadsPinned;
            return kFirstAttemptCount;
        }
        // cfg attemptcount (config.hpp): the same count on every attempt
        if (g_attemptCount >= 0) {
            ++g_attemptReadsPinned;
            return (double)g_attemptCount;
        }
        return v;
    }
};

class $modify(AttemptPinEffects, GJEffectManager) {
    void transferPersistentItems() {
        if (attemptPinActive()) {
            ++g_persistTransfersSkipped;
            return;
        }
        GJEffectManager::transferPersistentItems();
    }
};

#endif
