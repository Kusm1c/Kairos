#include "ProgressPanel.hpp"
#include "UiText.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace kp {

namespace {
constexpr float kW = 380.f, kH = 60.f, kBarH = 6.f;

std::string clock(double s) {
    int t = (int)std::lround(std::max(0.0, s));
    return t >= 60 ? fmt::format("{}:{:02}", t / 60, t % 60) : fmt::format("{} s", t);
}
} // namespace

ProgressPanel* ProgressPanel::create(const std::string& title) {
    auto* p = new ProgressPanel();
    if (p->init(title)) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

bool ProgressPanel::init(const std::string& title) {
    if (!CCNode::init()) return false;
    const auto win = CCDirector::get()->getWinSize();
    m_width = std::min(kW, win.width - 20.f);
    this->setContentSize({m_width, kH});
    this->setPosition({(win.width - m_width) / 2.f, win.height - kH - 6.f});

    auto* bg = CCLayerColor::create({8, 10, 20, 190}, m_width, kH);
    this->addChild(bg);
    auto* edge = CCLayerColor::create({90, 220, 255, 200}, m_width, 1.5f);
    edge->setPosition({0.f, kH - 1.5f});
    this->addChild(edge);

    auto* t = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
    fitLabel(t, m_width * 0.6f, 0.38f, 0.3f);   // the time left keeps the right side
    t->setAnchorPoint({0.f, 0.5f});
    t->setPosition({10.f, kH - 13.f});
    this->addChild(t);

    m_right = CCLabelBMFont::create("", "chatFont.fnt");
    m_right->setScale(0.55f);
    m_right->setAnchorPoint({1.f, 0.5f});
    m_right->setPosition({m_width - 10.f, kH - 13.f});
    this->addChild(m_right);

    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setScale(0.52f);
    m_status->setAnchorPoint({0.f, 0.5f});
    m_status->setPosition({10.f, kH - 30.f});
    this->addChild(m_status);

    m_track = CCLayerColor::create({40, 44, 60, 255}, m_width - 20.f, kBarH);
    m_track->setPosition({10.f, 12.f});
    this->addChild(m_track);
    m_fill = CCLayerColor::create({90, 220, 255, 255}, 0.f, kBarH);
    m_fill->setPosition({10.f, 12.f});
    this->addChild(m_fill);

    m_detail = CCLabelBMFont::create("Esc, then Quit: stop", "chatFont.fnt");
    m_detail->setScale(0.42f);
    m_detail->setAnchorPoint({1.f, 0.5f});
    m_detail->setPosition({m_width - 10.f, 5.f});
    m_detail->setOpacity(150);
    this->addChild(m_detail);
    this->scheduleUpdate();
    return true;
}

void ProgressPanel::set(const std::string& status, double fraction, double elapsed, const std::string& detail) {
    m_status->setString(status.c_str());
    fitLabel(m_status, m_width - 20.f, 0.52f);
    m_unknown = fraction < 0;
    if (!m_unknown) {
        const double f = std::clamp(fraction, 0.0, 1.0);
        m_fill->setPosition({10.f, 12.f});
        m_fill->changeWidth((float)f * (m_width - 20.f));
        std::string left = f > 0.02 ? "  |  " + clock(elapsed / f - elapsed) + " left" : "";
        m_right->setString(fmt::format("{:.0f}%{}", f * 100.0, left).c_str());
    } else {
        m_right->setString(clock(elapsed).c_str());
    }
    m_detail->setString((m_stop ? detail : detail.empty() ? std::string("Esc, then Quit: stop")
                                                      : detail + "   |   Esc, then Quit: stop")
                            .c_str());
    fitLabel(m_detail, m_width - 20.f, 0.42f, 0.38f);
}

void ProgressPanel::onStop(std::function<void()> stop) {
    m_stop = std::move(stop);
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu);
    auto* b = CCMenuItemSpriteExtra::create(ButtonSprite::create("Stop", "bigFont.fnt", "GJ_button_06.png", 0.35f), this,
                                            menu_selector(ProgressPanel::stopClicked));
    b->setPosition({m_width - 30.f, -14.f});
    menu->addChild(b);
    m_detail->setString("");
}

void ProgressPanel::stopClicked(CCObject*) {
    if (m_stop) m_stop();
}

void ProgressPanel::update(float dt) {
    if (!m_unknown) return;
    // A short segment sweeping the track.
    const float w = m_width - 20.f, seg = w * 0.18f;
    m_phase = std::fmod(m_phase + dt * 0.6f, 1.f);
    const float x = (w + seg) * m_phase - seg;
    m_fill->setPosition({10.f + std::max(0.f, x), 12.f});
    m_fill->changeWidth(std::clamp(std::min(seg, x + seg), 0.f, w - std::max(0.f, x)));
}

} // namespace kp
