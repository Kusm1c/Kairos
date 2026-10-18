#include "mod/playlayer_helpers.hpp"

using namespace p1;

namespace {
bool envActive() {
    return g_cfg.areaEnv && grouptrace::g_on && g_started && !g_sessionOver
           && !secsolve::g_active;
}

bool rotScaleVaries(EnterEffectInstance* inst) {
    using namespace areaenv;
    return fld<float>(inst, kLenV) != 0.f || fld<float>(inst, kOffV) != 0.f
           || fld<float>(inst, kOffYV) != 0.f || fld<float>(inst, kRotV) != 0.f
           || fld<float>(inst, kScaleXV) != 0.f || fld<float>(inst, kScaleYV) != 0.f;
}

#ifndef GEODE_IS_WINDOWS
bool moveVaries(EnterEffectInstance* inst) {
    using namespace areaenv;
    return fld<float>(inst, kLenV) != 0.f || fld<float>(inst, kOffV) != 0.f
           || fld<float>(inst, kOffYV) != 0.f || fld<float>(inst, kDistV) != 0.f
           || fld<float>(inst, kAngleV) != 0.f || fld<float>(inst, kMoveXV) != 0.f
           || fld<float>(inst, kMoveYV) != 0.f;
}
#endif
}  // namespace

class $modify(AreaEnvLayer, GJBaseGameLayer) {
#ifdef GEODE_IS_WINDOWS
    void processAreaMoveGroupAction(cocos2d::CCArray* objects, EnterEffectInstance* instance,
                                    cocos2d::CCPoint position, int outerMin, int outerMax,
                                    int middleMin, int middleMax, int startIndex,
                                    bool targetGroups, bool reset) {
        const bool on = envActive() && instance
                        && areaenv::fld<void*>(instance, areaenv::kTrigger) != nullptr;
        if (on) {
            areaenv::g_act = areaenv::Action{true, this, instance,
                                             areaenv::fld<void*>(instance, areaenv::kTrigger),
                                             position, targetGroups};
        }
        GJBaseGameLayer::processAreaMoveGroupAction(objects, instance, position, outerMin,
                                                    outerMax, middleMin, middleMax, startIndex,
                                                    targetGroups, reset);
        if (on) {
            areaenv::settle();
            areaenv::g_act = areaenv::Action{};
        }
    }

    bool resetAreaObjectValues(GameObject* object, bool update) {
        const bool r = GJBaseGameLayer::resetAreaObjectValues(object, update);
        if (areaenv::g_act.on && areaenv::g_act.l == this && areaenv::g_inMove == 0 && object)
            areaenv::onProcessed(object);
        return r;
    }

    void moveAreaObject(GameObject* object, float dx, float dy) {
        ++areaenv::g_inMove;   // it resets its target itself; that is not a new object
        GJBaseGameLayer::moveAreaObject(object, dx, dy);
        --areaenv::g_inMove;
        if (areaenv::g_act.on && areaenv::g_act.l == this && object)
            areaenv::onMove(object, dx, dy);
    }
#else
    void processAreaMoveGroupAction(cocos2d::CCArray* objects, EnterEffectInstance* instance,
                                    cocos2d::CCPoint position, int outerMin, int outerMax,
                                    int middleMin, int middleMax, int startIndex,
                                    bool targetGroups, bool reset) {
        if (envActive() && instance && objects && objects->count() > 0 && moveVaries(instance))
            ++areaenv::g_unenveloped;
        GJBaseGameLayer::processAreaMoveGroupAction(objects, instance, position, outerMin,
                                                    outerMax, middleMin, middleMax, startIndex,
                                                    targetGroups, reset);
    }
#endif

    void processAreaRotateGroupAction(cocos2d::CCArray* objects, EnterEffectInstance* instance,
                                      cocos2d::CCPoint position, int outerMin, int outerMax,
                                      int middleMin, int middleMax, int startIndex,
                                      bool targetGroups, bool reset) {
        if (envActive() && instance && objects && objects->count() > 0
            && rotScaleVaries(instance))
            ++areaenv::g_unenveloped;
        GJBaseGameLayer::processAreaRotateGroupAction(objects, instance, position, outerMin,
                                                      outerMax, middleMin, middleMax,
                                                      startIndex, targetGroups, reset);
    }

    void processAreaTransformGroupAction(cocos2d::CCArray* objects, EnterEffectInstance* instance,
                                         cocos2d::CCPoint position, int outerMin, int outerMax,
                                         int middleMin, int middleMax, int startIndex,
                                         bool targetGroups, bool reset) {
        if (envActive() && instance && objects && objects->count() > 0
            && rotScaleVaries(instance))
            ++areaenv::g_unenveloped;
        GJBaseGameLayer::processAreaTransformGroupAction(objects, instance, position, outerMin,
                                                         outerMax, middleMin, middleMax,
                                                         startIndex, targetGroups, reset);
    }
};
