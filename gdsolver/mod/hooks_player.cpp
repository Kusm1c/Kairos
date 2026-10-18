// EnhancedGameObject (pad trace) and PlayerObject hooks: state dump, physics tracing.
#include "mod/playlayer_helpers.hpp"
#include <Geode/modify/EndTriggerGameObject.hpp>

using namespace p1;

static bool solverSpiderPad() {
    bool on = true;
    for (const auto& a : g_cfg.dpArgs) {
        if (a == "--spiderpad") on = true;
        else if (a == "--no-spiderpad") on = false;
    }
    return on;
}

class $modify(PadTraceGameObject, EnhancedGameObject) {
    void activatedByPlayer(PlayerObject* p) {
        auto* eff = typeinfo_cast<EffectGameObject*>(this);
        if (eff && eff->m_isTouchTriggered) {
            ++touchseed::g_calls;
            auto* l = GJBaseGameLayer::get();
            const bool isP1 = (l && p == l->m_player1);
            if (isP1) {
                auto& m = touchseed::g_first;
                if (m.find(this->m_uniqueID) == m.end())
                    m[this->m_uniqueID] = (int)g_tick;
            } else if (l) {
                // Counted, never carried -- see touchseed::g_p2.
                ++touchseed::g_p2;
            }
        }
        {
            const int ty = (int)this->m_objectType;
            if (ty == 3 || ty == 4 || ty == 28) {
                ++portalseed::g_calls;
                auto* l = GJBaseGameLayer::get();
                auto& m = (l && p == l->m_player1) ? portalseed::g_first
                                                   : portalseed::g_first2;
                if (m.find(this->m_uniqueID) == m.end())
                    m[this->m_uniqueID] = (int)g_tick;
            }
            const bool spiderPadSeed = ty == 44 && solverSpiderPad();
            if (ty == 8 || ty == 9 || ty == 10 || ty == 34 || spiderPadSeed) {
                ++padseed::g_calls;
                auto* l = GJBaseGameLayer::get();
                if (l && p == l->m_player1) {
                    auto& m = padseed::g_first;
                    if (m.find(this->m_uniqueID) == m.end())
                        m[this->m_uniqueID] = (int)g_tick;
                }
            }
        }
        if (g_cfg.padTrace) {
            const int ty = (int)this->m_objectType;
            if (ty == 8 || ty == 9 || ty == 10) {
                auto* l = GJBaseGameLayer::get();
                if (l && p == l->m_player1 && ++padtrace::g_lines <= 400) {
                    auto r = this->getObjectRect();
                    auto pr = p->getObjectRect();
                    char buf[384];
                    snprintf(buf, sizeof(buf),
                        "padact: t=%lld id=%d uid=%d ty=%d o=(%.3f,%.3f) "
                        "p=(%.4f,%.4f) vy=%.4f used=%d orect=(%.4f,%.4f,%.4f,%.4f) "
                        "prect=(%.4f,%.4f,%.4f,%.4f) rot=%.3f prot=%.3f",
                        (long long)g_tick, this->m_objectID, this->m_uniqueID,
                        ty, this->getPositionX(), this->getPositionY(),
                        p->getPositionX(), p->getPositionY(),
                        (float)p->m_yVelocity,
                        this->m_activatedByPlayer1 ? 1 : 0,
                        r.origin.x, r.origin.y, r.size.width, r.size.height,
                        pr.origin.x, pr.origin.y, pr.size.width, pr.size.height,
                        (float)this->getRotation(), (float)p->getRotation());
                    writeResult(buf);
                }
            }
        }
        EnhancedGameObject::activatedByPlayer(p);
    }
};

class $modify(PlayerObject) {
    void playDeathEffect() {
        if (!g_deathFx && renderSuppressed()) { ++g_deathFxSkipped; return; }
        PlayerObject::playDeathEffect();
    }

    // cfg `presstrace=t0,t1`: the press latch bytes, the ground flag and vy (see g_pressT0).
    void pressLine(const char* where) {
        if (g_tick < g_pressT0 || g_tick > g_pressT1 || !g_started || g_sessionOver) return;
        const unsigned char* raw = reinterpret_cast<const unsigned char*>(this);
        char b[200];
        snprintf(b, sizeof(b),
                 "press: t=%lld at=%s b985=%d b986=%d b98a=%d onGround=%d vy=%.4f y=%.4f",
                 (long long)g_tick, where, (int)raw[gdoff::kPlayerJumpBuffered],
                 (int)raw[gdoff::kPlayerStateRingJump], (int)raw[gdoff::kPlayerStateRingJump2],
                 (int)raw[gdoff::kPlayerOnGround], (double)this->m_yVelocity, this->getPositionY());
        writeResult(b);
    }

    void update(float dt) {
        auto* l = GJBaseGameLayer::get();
        bool isP1 = l && this == l->m_player1;
        if (isP1) ev("PO_update_pre", dt);
        if (isP1) pressLine("update-in");
        PlayerObject::update(dt);
        if (isP1) pressLine("update-out");
        if (isP1) traceModifierCounters(this);
    }

    void stickLine(const char* where) {
        if (g_tick < g_stickT0 || g_tick > g_stickT1 || !g_started || g_sessionOver) return;
        const unsigned char* raw = reinterpret_cast<const unsigned char*>(this);
        auto uidAt = [raw](size_t off) -> int {
            auto* o = *reinterpret_cast<GameObject* const*>(raw + off);
            return o ? o->m_uniqueID : -1;
        };
        const long long q0 = *reinterpret_cast<const long long*>(raw + gdoff::kPlayerCollisionLogBottom);
        const long long q1 = *reinterpret_cast<const long long*>(raw + gdoff::kPlayerCollisionLogLeft);
        char b[288];
        snprintf(b, sizeof(b),
                 "stick: t=%lld dt=%lld at=%s g608=%d s600=%d f658=%d slope=%d q5b8=%lld "
                 "ax=%d og=%d x=%.4f y=%.4f vy=%.4f",
                 (long long)g_tick, (long long)g_tick + 1, where, uidAt(gdoff::kPlayerLastGroundObject),
                 uidAt(gdoff::kPlayerCollidedObject), (int)raw[gdoff::kPlayerCollidingWithSlope],
                 uidAt(gdoff::kPlayerCurrentSlope2), q1 - q0, (int)raw[gdoff::kPlayerSideways],
                 (int)raw[gdoff::kPlayerOnGround],
                 this->getPositionX(), this->getPositionY(), (double)this->m_yVelocity);
        writeResult(b);
    }

    void postCollision(float dt, bool betweenSteps) {
        auto* l = GJBaseGameLayer::get();
        const bool isP1 = l && this == l->m_player1;
        if (isP1) { stickLine("post-in"); g_inPostCollP1 = true; }
        PlayerObject::postCollision(dt, betweenSteps);
        if (isP1) { g_inPostCollP1 = false; stickLine("post-out"); }
    }

    void spiderTestJump(bool dynamic) {
        auto* l = GJBaseGameLayer::get();
        const bool isP1 = l && this == l->m_player1;
        const float y0 = this->getPositionY();
        PlayerObject::spiderTestJump(dynamic);
        if (!isP1 || !g_cfg.standTrace || !g_started || g_sessionOver) return;
        auto* tg = *reinterpret_cast<GameObject**>((char*)this + gdoff::kPlayerCurrentSlope2);
        const double lo = *reinterpret_cast<double*>((char*)this + gdoff::kPlayerCollidedBottomMaxY);
        const double hi = *reinterpret_cast<double*>((char*)this + gdoff::kPlayerCollidedLeftMaxX);
        char tb[192] = "-";
        if (tg) {
            auto q = tg->getPosition();
            auto r = tg->getObjectRect();
            snprintf(tb, sizeof(tb),
                     "uid%d id%d type%d @%.2f,%.2f %.2fx%.2f",
                     tg->m_uniqueID, tg->m_objectID, (int)tg->m_objectType,
                     q.x, q.y, r.size.width, r.size.height);
        }
        char b[320];
        snprintf(b, sizeof(b),
                 "spidertp: t=%lld dyn=%d y %.3f -> %.3f up=%d lo=%.3f hi=%.3f "
                 "tgt=[%s]",
                 (long long)g_tick, (int)dynamic, y0, this->getPositionY(),
                 (int)this->m_isUpsideDown, lo, hi, tb);
        writeResult(b);
    }

    void didHitHead() {
        auto* l = GJBaseGameLayer::get();
        const bool watch = g_cfg.hitboxTrace && l && this == l->m_player1
                           && g_started && !g_sessionOver;
        if (watch) {
            static int lines = 0;
            if (++lines <= 400) {
                const char* pb = reinterpret_cast<const char*>(this);
                int b80 = 0, b74 = 0; float b84 = 0.f;
                memcpy(&b80, pb + gdoff::kPlayerStateFlipGravity, 4);
                memcpy(&b74, pb + gdoff::kPlayerStateNoAutoJump, 4);
                memcpy(&b84, pb + gdoff::kPlayerGravityMod, 4);
                const char* self = pb;
                char o[160];
                snprintf(o, sizeof(o),
                         "dhhoff: stateFlipGravity=0x%X gravityMod=0x%X "
                         "gravity=0x%X isUpsideDown=0x%X",
                         (unsigned)((const char*)&this->m_stateFlipGravity - self),
                         (unsigned)((const char*)&this->m_gravityMod - self),
                         (unsigned)((const char*)&this->m_gravity - self),
                         (unsigned)((const char*)&this->m_isUpsideDown - self));
                writeResult(o);
                char b[224];
                snprintf(b, sizeof(b),
                         "dhh: t=%lld x=%.3f y=%.3f vy=%.3f up=%d gate[b80]=%d "
                         "[b74]=%d [b84]=%.4f topMinY=%.3f",
                         (long long)g_tick, this->getPositionX(),
                         this->getPositionY(), this->m_yVelocity,
                         (int)this->m_isUpsideDown, b80, b74, b84,
                         this->m_collidedTopMinY);
                writeResult(b);
            }
        }
        PlayerObject::didHitHead();
    }

    void flipGravity(bool flip, bool noEffects) {
        auto* l = GJBaseGameLayer::get();
        const bool watch = g_cfg.hitboxTrace && l && this == l->m_player1
                           && g_started && !g_sessionOver;
        const int before = watch ? (int)this->m_isUpsideDown : 0;
        PlayerObject::flipGravity(flip, noEffects);
        if (!watch) return;
        static int lines = 0;
        if (++lines > 2000) return;
#ifdef GEODE_IS_WINDOWS
        {
            void* fr[24];
            const USHORT n = RtlCaptureStackBackTrace(0, 24, fr, nullptr);
            const auto base = (uintptr_t)GetModuleHandleA(nullptr);
            std::string cs;
            for (USHORT i = 0; i < n; ++i) {
                const uintptr_t a = (uintptr_t)fr[i];
                char t[32];
                if (a >= base && a - base < 0x600000)
                    snprintf(t, sizeof(t), " gd:%llX",
                             (unsigned long long)(a - base));
                else
                    snprintf(t, sizeof(t), " ?%llX", (unsigned long long)a);
                cs += t;
            }
            writeResult(("pfgstk: t=" + std::to_string((long long)g_tick)
                         + " n=" + std::to_string((int)n)
                         + " base=" + std::to_string((long long)base)
                         + cs).c_str());
        }
#endif
        char b[224];
        snprintf(b, sizeof(b),
                 "pfg: t=%lld flip=%d noFx=%d up %d->%d x=%.3f y=%.3f vy=%.3f "
                 "slopeUp=%d slopeRel=%d onSlope=%d grav=%.3f",
                 (long long)g_tick, flip ? 1 : 0, noEffects ? 1 : 0,
                 before, (int)this->m_isUpsideDown,
                 this->getPositionX(), this->getPositionY(), this->m_yVelocity,
                 (int)this->m_maybeUpsideDownSlope,
                 (int)this->m_slopeFlipGravityRelated,
                 (int)this->m_isOnSlope, (double)this->m_gravity);
        writeResult(b);
    }

    void runNormalRotation(bool notNormalMode, float speed) {
        auto* l = GJBaseGameLayer::get();
        const bool watch = g_cfg.hitboxTrace && l && this == l->m_player1
                           && g_started && !g_sessionOver;
        const float before = watch ? this->m_rotationSpeed : 0.f;
        PlayerObject::runNormalRotation(notNormalMode, speed);
        if (!watch) return;
        static int lines = 0;
        if (++lines > 4000) return;
        char b[192];
        snprintf(b, sizeof(b),
                 "rnr: t=%lld nnm=%d speed=%.4f rspd %.4f -> %.4f "
                 "flip=%d grounded=%d size=%.3f",
                 (long long)g_tick, notNormalMode ? 1 : 0, speed,
                 before, this->m_rotationSpeed,
                 (int)this->m_isUpsideDown, (int)this->m_isOnGround,
                 this->m_vehicleSize);
        writeResult(b);
    }

    void checkSnapJumpToObject(GameObject* object) {
        auto* l = GJBaseGameLayer::get();
        if (g_cfg.snapNodeLog && object && l && this == l->m_player1) {
            const CCPoint node = object->getPosition();
            const float rx = (float)object->m_positionX, ry = (float)object->m_positionY;
            if (node.x != rx || node.y != ry) {
                char b[200];
                snprintf(b, sizeof(b), "snapnode: t=%lld uid=%d node=(%.3f,%.3f) real=(%.3f,%.3f) dx=%+.3f "
                         "vis=%s sync=%d", (long long)g_tick, object->m_uniqueID, node.x, node.y, rx, ry,
                         rx - node.x, visibilityPassOff() ? "off" : "on",
                         (g_cfg.snapNodeSync && visibilityPassOff()) ? 1 : 0);
                writeResult(b);
            }
        }
        if (g_cfg.snapNodeSync && object && visibilityPassOff()) {
            const CCPoint real((float)object->m_positionX, (float)object->m_positionY);
            const CCPoint node = object->getPosition();
            if (node.x != real.x || node.y != real.y) object->setPosition(real);
        }
        bool watch = g_cfg.snapTrace && l && this == l->m_player1 && object;
        if (!watch) { PlayerObject::checkSnapJumpToObject(object); return; }
        GameObject* prev = this->m_objectSnappedTo;
        CCPoint pp = prev ? prev->getRealPosition() : CCPoint(0, 0);
        CCPoint op = object->getRealPosition();
        float x0 = this->getPositionX();
        double sd0 = this->m_snapDistance;
        PlayerObject::checkSnapJumpToObject(object);
        float x1 = this->getPositionX();
        static int lines = 0;
        if (++lines > 40000) return;
        char b[320];
        snprintf(b, sizeof(b),
            "snap: t=%lld y=%.3f spd=%.1f size=%.2f flip=%d "
            "prev=%d,type=%d,(%.2f,%.2f) obj=%d,type=%d,(%.2f,%.2f) "
            "dx=%.2f dy=%.2f x=%.4f->%.4f d=%+.4f sd=%.4f->%.4f",
            (long long)g_tick, this->getPositionY(), (float)this->m_playerSpeed,
            this->m_vehicleSize, this->m_isUpsideDown ? 1 : 0,
            prev ? prev->m_uniqueID : -1, prev ? (int)prev->getType() : -1, pp.x, pp.y,
            object->m_uniqueID, (int)object->getType(), op.x, op.y,
            prev ? op.x - pp.x : 0.f, prev ? op.y - pp.y : 0.f,
            x0, x1, x1 - x0, sd0, this->m_snapDistance);
        writeResult(b);
    }

    void ringJump(RingObject* object, bool skipCheck) {
        auto* l = GJBaseGameLayer::get();
        bool isP1 = object && l && this == l->m_player1;
        // ...and p2 (ringseed::g_fired2, dp --spentorb2): the same latches on its own object
        const bool isP2 = object && l && !isP1 && this == l->m_player2;
        const unsigned char* const fireLatch = (const unsigned char*)this + gdoff::kPlayerTouchedRing;
        auto latchBits = [&]() {
            return (fireLatch[0] ? 1 : 0) | (fireLatch[1] ? 2 : 0) | (fireLatch[2] ? 4 : 0);
        };
        const int latchBefore = (isP1 || isP2) ? latchBits() : 0;
        auto noteRingFire = [&]() {
            if (isP2) {
                if ((latchBits() & ~latchBefore) == 0) return;
                const int uid = object->m_uniqueID;
                for (const auto& f : ringseed::g_fired2)
                    if (f.first == uid) return;
                ringseed::g_fired2.push_back({uid, (int)g_tick});
                return;
            }
            if (!isP1) return;
            ++ringseed::g_calls;
            if ((latchBits() & ~latchBefore) == 0) return;
            const int uid = object->m_uniqueID;
            for (const auto& f : ringseed::g_fired)
                if (f.first == uid) return;
            ringseed::g_fired.push_back({uid, (int)g_tick});
        };
        const bool inWin = g_tick >= g_cfg.hbFrom
                        && (g_cfg.hbTo <= 0 || g_tick <= g_cfg.hbTo);
        unsigned char* const spentMirror = (unsigned char*)this + gdoff::kPlayerStateRingJump2;
        const bool subst = g_cfg.subRingSpent && isP1 && inWin;
        const unsigned char savedSpent = subst ? *spentMirror : (unsigned char)0;
        if (subst) {
            *spentMirror = 1;
            if (orbtrace::g_lines < 400) {
                char sb[128];
                snprintf(sb, sizeof(sb),
                         "subst: t=%lld uid=%d 0x98a %d->1 NOT-GD",
                         (long long)g_tick, object->m_uniqueID,
                         (int)savedSpent);
                writeResult(sb);
                ++orbtrace::g_lines;
            }
        }
        bool watch = g_cfg.orbTrace && isP1;
        if (!watch) {
            PlayerObject::ringJump(object, skipCheck);
            if (subst) *spentMirror = savedSpent;
            noteRingFire();
            return;
        }
        float ox = object->getPositionX(), oy = object->getPositionY();
        float vyBefore = (float)this->m_yVelocity;
        if (inWin && orbtrace::g_lines < 400) {
            auto bit = [&](int off) {
                return (int)*((unsigned char*)this + off);
            };
            char cb[256];
            snprintf(cb, sizeof(cb),
                "orbcall: t=%lld uid=%d id=%d vy=%.4f "
                "b989=%d b98a=%d b98b=%d b98c=%d b98d=%d b9e4=%d claim=%d",
                (long long)g_tick, object->m_uniqueID, object->m_objectID,
                vyBefore, bit(gdoff::kPlayerStateJumpBuffered), bit(gdoff::kPlayerStateRingJump2),
                bit(gdoff::kPlayerTouchedRing), bit(gdoff::kPlayerTouchedRing + 1),
                bit(gdoff::kPlayerTouchedRing + 2), bit(gdoff::kPlayerDashing),
                (int)*((unsigned char*)object + gdoff::kRingClaimTouch));
            writeResult(cb);
            ++orbtrace::g_lines;
        }
        PlayerObject::ringJump(object, skipCheck);
        if (subst) *spentMirror = savedSpent;
        noteRingFire();
        float vyAfter = (float)this->m_yVelocity;
        if (std::abs(vyAfter - vyBefore) < 1e-4f) return; // did not fire
        // Log-flood guard: only near the targeted x + a total cap
        if (g_cfg.orbTraceX > 0 && std::abs(ox - g_cfg.orbTraceX) > 300.f) return;
        if (++orbtrace::g_lines > 400) return;
        long long lag = (orbtrace::g_lastPress >= 0)
                      ? (long long)g_tick - orbtrace::g_lastPress : -1;
        // ...and whether THIS tick's press is still queued behind us (see above).
        const int pend = (g_nextInput < g_cfg.inputs.size()
                          && g_cfg.inputs[g_nextInput].step == g_tick
                          && g_cfg.inputs[g_nextInput].down) ? 1 : 0;
        auto rr = object->getObjectRect();
        auto pr = this->getObjectRect();
        char buf[352];
        snprintf(buf, sizeof(buf),
            "orb: id=%d uid=%d mode=%d size=%.2f flip=%d spd=%.1f "
            "t=%lld press=%lld lag=%lld pend=%d dxx=%.1f dyy=%.1f "
            "orect=(%.4f,%.4f) prc=(%.4f,%.4f) px=%.4f orc=%.4f "
            "orb=(%.0f,%.0f) vy=%.4f->%.4f",
            object->m_objectID, object->m_uniqueID, (int)solver::modeOf(this),
            this->m_vehicleSize, this->m_isUpsideDown ? 1 : 0,
            (float)this->m_playerSpeed,
            (long long)g_tick, (long long)orbtrace::g_lastPress, lag, pend,
            this->getPositionX() - ox, this->getPositionY() - oy,
            rr.size.width, rr.size.height,
            pr.origin.x + pr.size.width * 0.5f,
            pr.origin.y + pr.size.height * 0.5f,
            this->getPositionX(),
            rr.origin.x + rr.size.width * 0.5f,
            ox, oy, vyBefore, vyAfter);
        writeResult(buf);
    }

    void propellPlayer(float yVelocity, bool noEffects, int objectType) {
        auto* l = GJBaseGameLayer::get();
        bool watch = g_cfg.padTrace && l && this == l->m_player1;
        if (!watch) {
            PlayerObject::propellPlayer(yVelocity, noEffects, objectType); return;
        }
        float vyBefore = (float)this->m_yVelocity;
        PlayerObject::propellPlayer(yVelocity, noEffects, objectType);
        if (++padtrace::g_lines > 400) return;
        char buf[224];
        snprintf(buf, sizeof(buf),
            "padfire: t=%lld p=(%.4f,%.4f) arg=%.4f otype=%d "
            "vy=%.4f->%.4f flip=%d mode=%d spd=%.1f",
            (long long)g_tick, this->getPositionX(), this->getPositionY(),
            yVelocity, objectType, vyBefore, (float)this->m_yVelocity,
            this->m_isUpsideDown ? 1 : 0, (int)solver::modeOf(this),
            (float)this->m_playerSpeed);
        writeResult(buf);
    }

    const char* hbWho() const {
        auto* l = GJBaseGameLayer::get();
        if (!l) return nullptr;
        if (this == l->m_player1) return "p1";
        if (this == l->m_player2) return "p2";
        return nullptr;
    }

    bool collidedWithObject(float dt, GameObject* obj, cocos2d::CCRect rect, bool skip) {
        auto* l = GJBaseGameLayer::get();
        const char* who = hbWho();
        const bool hbWatch = g_cfg.hitboxTrace && who && obj
                             && g_tick >= g_cfg.hbFrom
                             && (g_cfg.hbTo <= 0 || g_tick <= g_cfg.hbTo);
        CCRect prePr = hbWatch ? this->getObjectRect() : CCRect();
        if (g_inPostCollP1 && dt == 0.f && obj && g_tick >= g_stickT0 && g_tick <= g_stickT1) {
            char sb[160];
            snprintf(sb, sizeof(sb), "stick: t=%lld dt=%lld at=reland uid=%d y=%.4f vy=%.4f",
                     (long long)g_tick, (long long)g_tick + 1, obj->m_uniqueID,
                     this->getPositionY(), (double)this->m_yVelocity);
            writeResult(sb);
        }
        bool r = PlayerObject::collidedWithObject(dt, obj, rect, skip);
        if (hbWatch) {
            static int lines = 0;
            if (++lines <= 40000) {
                CCRect pr = this->getObjectRect();
                CCRect orr = obj->getObjectRect();
                GameObject* cs = static_cast<GameObject*>(this->m_currentSlope);
                char b[416];
                snprintf(b, sizeof(b),
                    "hbox: t=%lld dt=%lld who=%s obj=%d type=%d hit=%d size=%.2f "
                    "arg=(%.2f,%.2f,%.2f,%.2f) objrect=(%.2f,%.2f,%.2f,%.2f) "
                    "player=(%.2f,%.2f,%.2f,%.2f) ppre=(%.2f,%.2f,%.2f,%.2f) "
                    "onslp=%d wasslp=%d curslp=%d",
                    (long long)g_tick, (long long)g_tick + 1, who,
                    obj->m_uniqueID, (int)obj->getType(),
                    r ? 1 : 0, this->m_vehicleSize,
                    rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
                    orr.origin.x, orr.origin.y, orr.size.width, orr.size.height,
                    pr.origin.x, pr.origin.y, pr.size.width, pr.size.height,
                    prePr.origin.x, prePr.origin.y,
                    prePr.size.width, prePr.size.height,
                    (int)this->m_isOnSlope, (int)this->m_wasOnSlope,
                    cs ? cs->m_uniqueID : -1);
                writeResult(b);
            }
        }
        return r;
    }

    void collidedWithSlopeInternal(float dt, GameObject* obj, bool forced) {
        const char* who = hbWho();
        const bool watch = g_cfg.hitboxTrace && who && obj
                           && g_started && !g_sessionOver
                           && g_tick >= g_cfg.hbFrom
                           && (g_cfg.hbTo <= 0 || g_tick <= g_cfg.hbTo);
        const double yBefore = watch ? this->getPositionY() : 0.0;
        const double vyBefore = watch ? (double)this->m_yVelocity : 0.0;
        const int oS0 = watch ? (int)this->m_isOnSlope : 0;
        const int top0 = watch ? (int)this->m_isCurrentSlopeTop : 0;
        const int was0 = watch ? (int)this->m_wasOnSlope : 0;
        const int jb0 = watch ? (int)this->m_jumpBuffered : 0;
        const int ud0 = watch ? (int)this->m_maybeUpsideDownSlope : 0;
        const double st0 = watch ? this->m_slopeStartTime : 0.0;
        CCRect prr = watch ? this->getObjectRect() : CCRect();
        const double syAtX0 = watch ? obj->slopeYPos(this->getPositionX()) : 0.0;
        PlayerObject::collidedWithSlopeInternal(dt, obj, forced);
        if (!watch) return;
        static int lines = 0;
        if (++lines > 4000) return;
        CCRect orr = obj->getObjectRect();
        const unsigned char* raw = reinterpret_cast<const unsigned char*>(this);
        const unsigned char p986 = *(raw + gdoff::kPlayerStateRingJump);
        const unsigned char b68c = *(raw + gdoff::kPlayerSlopeFlipGravity);
        const unsigned char b985 = *(raw + gdoff::kPlayerJumpBuffered);
        char b[640];
        snprintf(b, sizeof(b),
                 "slp: t=%lld dt=%lld who=%s uid=%d id=%d forced=%d onSlope=%d up=%d top=%d "
                 "y %.3f->%.3f "
                 "vy=%.3f rect=(%.2f,%.2f,%.2f,%.2f) rot=%.1f syAtX=%.3f "
                 "held=%d p986=%d p9b8=%d vyin=%.3f dvy=%.3f "
                 "b68c=%d b985=%d jb=%d "
                 "| IN oS0=%d top0=%d was0=%d jb0=%d ud0=%d st0=%.3f "
                 "prect=(%.2f,%.2f,%.2f,%.2f) syAtX0=%.3f",
                 (long long)g_tick, (long long)g_tick + 1, who,
                 obj->m_uniqueID, obj->m_objectID,
                 forced ? 1 : 0, (int)this->m_isOnSlope,
                 (int)this->m_isUpsideDown, (int)this->m_isCurrentSlopeTop,
                 yBefore, this->getPositionY(), this->m_yVelocity,
                 orr.origin.x, orr.origin.y, orr.size.width, orr.size.height,
                 obj->getRotation(), obj->slopeYPos(this->getPositionX()),
                 g_btnDown ? 1 : 0, (int)p986,
                 (int)this->m_maybeUpsideDownSlope,
                 vyBefore, (double)this->m_yVelocity - vyBefore,
                 (int)b68c, (int)b985, (int)this->m_jumpBuffered,
                 oS0, top0, was0, jb0, ud0, st0,
                 prr.origin.x, prr.origin.y, prr.size.width, prr.size.height,
                 syAtX0);
        writeResult(b);
    }

    bool collidedWithObjectInternal(float dt, GameObject* obj, cocos2d::CCRect rect,
                                    bool skip) {
        bool r = PlayerObject::collidedWithObjectInternal(dt, obj, rect, skip);
        const char* who = hbWho();
        if (g_cfg.hitboxTrace && who && obj
            && g_tick >= g_cfg.hbFrom
            && (g_cfg.hbTo <= 0 || g_tick <= g_cfg.hbTo)) {
            static int lines = 0;
            if (++lines <= 40000) {
                CCRect pr = this->getObjectRect();
                CCRect orr = obj->getObjectRect();
                char b[416];
                snprintf(b, sizeof(b),
                    "hbin: t=%lld who=%s obj=%d type=%d hit=%d size=%.2f "
                    "arg=(%.2f,%.2f,%.2f,%.2f) objrect=(%.2f,%.2f,%.2f,%.2f) "
                    "player=(%.2f,%.2f,%.2f,%.2f)",
                    (long long)g_tick, who, obj->m_uniqueID, (int)obj->getType(),
                    r ? 1 : 0, this->m_vehicleSize,
                    rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
                    orr.origin.x, orr.origin.y, orr.size.width, orr.size.height,
                    pr.origin.x, pr.origin.y, pr.size.width, pr.size.height);
                writeResult(b);
            }
        }
        if (g_cfg.fieldProbe && who && obj
            && g_tick >= g_cfg.hbFrom
            && (g_cfg.hbTo <= 0 || g_tick <= g_cfg.hbTo)) {
            static int fpLines = 0;
            if (++fpLines <= 40000) {
                const char* ob = reinterpret_cast<const char*>(obj);
                const char* pb = reinterpret_cast<const char*>(this);
                const int o515 = (int)(unsigned char)ob[gdoff::kObjPassable];
                float s394 = 0.f, s398 = 0.f;
                memcpy(&s394, pb + gdoff::kPlayerSpriteWidthScale, sizeof(float));
                memcpy(&s398, pb + gdoff::kPlayerSpriteHeightScale, sizeof(float));
                const int plat = (int)(unsigned char)pb[gdoff::kPlayerPlatformer];
                const int c44 = (int)(unsigned char)pb[gdoff::kPlayerIgnoreDamage];
                const unsigned ofsUp = (unsigned)(
                    reinterpret_cast<const char*>(&this->m_isUpsideDown) - pb);
                char fb[288];
                snprintf(fb, sizeof(fb),
                    "fprobe: t=%lld who=%s obj=%d id=%d hit=%d size=%.2f "
                    "o515=%d p394=%.4f p398=%.4f plat=%d c44=%d "
                    "ofs=(0x%x,0x%x,0x%x,0x%x,0x%x) canary_up=0x%x want=0x%x %s",
                    (long long)g_tick, who, obj->m_uniqueID, obj->m_objectID,
                    r ? 1 : 0, this->m_vehicleSize,
                    o515, (double)s394, (double)s398, plat, c44,
                    (unsigned)gdoff::kObjPassable, (unsigned)gdoff::kPlayerSpriteWidthScale,
                    (unsigned)gdoff::kPlayerSpriteHeightScale, (unsigned)gdoff::kPlayerPlatformer,
                    (unsigned)gdoff::kPlayerIgnoreDamage, ofsUp,
                    (unsigned)gdoff::kPlayerUpsideDown,
                    ofsUp == gdoff::kPlayerUpsideDown ? "LAYOUT-OK" : "LAYOUT-MISMATCH");
                writeResult(fb);
            }
        }
        return r;
    }

    bool pushButton(PlayerButton button) {
        auto* l = GJBaseGameLayer::get();
        if (l && this == l->m_player1) ev("PO_pushButton", (int)button);
        const bool r = PlayerObject::pushButton(button);
        if (l && this == l->m_player1) pressLine("pushButton-out");
        return r;
    }

    bool releaseButton(PlayerButton button) {
        auto* l = GJBaseGameLayer::get();
        if (l && this == l->m_player1) ev("PO_releaseButton", (int)button);
        return PlayerObject::releaseButton(button);
    }
};

class $modify(EndTrigHook, EndTriggerGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, gd::vector<int> const* remapKeys) {
        if (g_started && solver::g_endTriggerFiredTick < 0) {
            solver::g_endTriggerFiredTick = (long long)g_tick;
            writeResult("endtrigger: uid " + std::to_string(this->m_uniqueID) + " fired at t="
                        + std::to_string(g_tick));
        }
        EndTriggerGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};
