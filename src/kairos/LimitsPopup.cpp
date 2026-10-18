#include "LimitsPopup.hpp"
#include "UiText.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

namespace kp {
namespace {

class LimitsPopup : public geode::Popup {
public:
    static LimitsPopup* create(const std::string& level, const std::string& why) {
        auto* p = new LimitsPopup();
        if (p->init(level, why)) {
            p->autorelease();
            return p;
        }
        delete p;
        return nullptr;
    }

private:
    static constexpr float kW = 340.f, kH = 210.f, kText = 0.62f;

    bool init(const std::string& level, const std::string& why) {
        if (!Popup::init(kW, kH)) return false;
        this->setTitle("Impossible with your limits");
        auto* name = CCLabelBMFont::create(level.c_str(), "goldFont.fnt");
        fitLabel(name, kW - 60.f, 0.5f, 0.35f);
        m_mainLayer->addChildAtPosition(name, Anchor::Top, ccp(0.f, -44.f));

        const float textW = kW - 40.f;
        auto* body = CCLabelBMFont::create(wrapText(why, "chatFont.fnt", kText, textW).c_str(), "chatFont.fnt");
        body->setAlignment(kCCTextAlignmentCenter);
        body->setScale(kText);
        // A long reason is shrunk as a block, never below a readable size.
        const float room = kH - 120.f;
        if (body->getScaledContentSize().height > room)
            body->setScale(std::max(0.45f, kText * room / body->getScaledContentSize().height));
        m_mainLayer->addChildAtPosition(body, Anchor::Center, ccp(0.f, 4.f));

        auto* hint = CCLabelBMFont::create("Raise them in the settings, or turn on Go crazy!", "chatFont.fnt");
        fitLabel(hint, textW, 0.5f);
        hint->setOpacity(160);
        m_mainLayer->addChildAtPosition(hint, Anchor::Bottom, ccp(0.f, 52.f));

        auto* ok = CCMenuItemSpriteExtra::create(ButtonSprite::create("OK", "goldFont.fnt", "GJ_button_01.png", 0.8f),
                                                 this, menu_selector(LimitsPopup::onOk));
        auto* settings = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Settings", "goldFont.fnt", "GJ_button_04.png", 0.8f), this,
            menu_selector(LimitsPopup::onSettings));
        m_buttonMenu->addChildAtPosition(ok, Anchor::Bottom, ccp(-60.f, 24.f));
        m_buttonMenu->addChildAtPosition(settings, Anchor::Bottom, ccp(60.f, 24.f));
        return true;
    }

    void onOk(CCObject*) { this->onClose(nullptr); }
    void onSettings(CCObject*) {
        this->onClose(nullptr);
        openSettingsPopup(Mod::get());
    }
};

} // namespace

void showLimitsPopup(const std::string& levelName, const std::string& why) {
    if (auto* p = LimitsPopup::create(levelName, why)) p->show();
}

} // namespace kp
