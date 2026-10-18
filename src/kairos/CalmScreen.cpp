#include "CalmScreen.hpp"
#include "GameEngine.hpp"
#include "ModGuard.hpp"
#include "UiText.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>

using namespace geode::prelude;

namespace kp {
namespace {
struct Calm {
    Ref<CCLayer> cover;
    PlayLayer* layer = nullptr;   // compared only, never touched once it is not PlayLayer::get()
} g;

void poll() {
    auto* pl = PlayLayer::get();
    const bool want = pl && pl->getParent() && Mod::get()->getSettingValue<bool>("hide-level") &&
                      (solverDriving() || GameEngine::active());
    if (g.cover && (!want || g.layer != pl)) {
        if (pl && g.layer == pl) pl->setVisible(true);
        g.cover->removeFromParent();
        g.cover = nullptr;
        g.layer = nullptr;
    }
    if (!want || g.cover) return;
    const auto win = CCDirector::get()->getWinSize();
    // GD's menu background.
    auto* cover = CCLayer::create();
    auto* bg = CCSprite::create("GJ_gradientBG.png");
    bg->setScaleX(win.width / bg->getContentSize().width);
    bg->setScaleY(win.height / bg->getContentSize().height);
    bg->setPosition({win.width / 2.f, win.height / 2.f});
    bg->setColor({0, 102, 255});
    cover->addChild(bg);
    auto* note = CCLabelBMFont::create("Level hidden while the bot searches", "goldFont.fnt");
    fitLabel(note, win.width - 32.f, 0.6f);
    note->setPosition(win.width / 2.f, 18.f);
    cover->addChild(note);
    pl->getParent()->addChild(cover, kCalmZ);
    pl->setVisible(false);
    g.cover = cover;
    g.layer = pl;
}
} // namespace
} // namespace kp

class $modify(KairosCalmScheduler, CCScheduler) {
    void update(float dt) {
        kp::poll();
        CCScheduler::update(dt);
    }
};
