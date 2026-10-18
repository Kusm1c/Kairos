#include "ProgressPanel.hpp"
#include "UiText.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace kp {

namespace {
constexpr float kW = 380.f, kH = 64.f;

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

    // GD's own popup and level progress bar.
    auto* bg = CCScale9Sprite::create("GJ_square01.png");
    bg->setContentSize({m_width, kH});
    bg->setAnchorPoint({0.f, 0.f});
    this->addChild(bg);

    auto* t = CCLabelBMFont::create(title.c_str(), "goldFont.fnt");
    fitLabel(t, m_width * 0.6f, 0.55f, 0.35f);   // the time left keeps the right side
    t->setAnchorPoint({0.f, 0.5f});
    t->setPosition({12.f, kH - 15.f});
    this->addChild(t);

    m_right = CCLabelBMFont::create("", "bigFont.fnt");
    m_right->setScale(0.3f);
    m_right->setAnchorPoint({1.f, 0.5f});
    m_right->setPosition({m_width - 12.f, kH - 15.f});
    this->addChild(m_right);

    m_status = CCLabelBMFont::create("", "bigFont.fnt");
    m_status->setScale(0.3f);
    m_status->setAnchorPoint({0.f, 0.5f});
    m_status->setPosition({12.f, kH - 32.f});
    this->addChild(m_status);

    const float barW = m_width - 24.f;
    m_track = CCSprite::create("GJ_progressBar_001.png");
    m_track->setColor({0, 0, 0});
    m_track->setOpacity(125);
    m_track->setScaleX(barW / m_track->getContentSize().width);
    m_track->setScaleY(0.7f);
    m_track->setAnchorPoint({0.f, 0.5f});
    m_track->setPosition({12.f, 15.f});
    this->addChild(m_track);
    m_fill = CCSprite::create("GJ_progressBar_001.png");
    m_fill->setColor({0, 255, 0});
    m_fill->setScaleX(m_track->getScaleX());
    m_fill->setScaleY(0.7f);
    m_fill->setAnchorPoint({0.f, 0.5f});
    this->addChild(m_fill);
    showFill(0.f, 0.f);

    m_detail = CCLabelBMFont::create("", "chatFont.fnt");
    m_detail->setScale(0.45f);
    m_detail->setAnchorPoint({0.f, 0.5f});
    m_detail->setPosition({12.f, 4.f});
    m_detail->setOpacity(170);
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
        showFill(0.f, (float)f);
        std::string left = f > 0.02 ? "  |  " + clock(elapsed / f - elapsed) + " left" : "";
        m_right->setString(fmt::format("{:.0f}%{}", f * 100.0, left).c_str());
    } else {
        m_right->setString(clock(elapsed).c_str());
    }
    m_detail->setString(detail.c_str());
    fitLabel(m_detail, m_width - (m_stop ? 90.f : 24.f), 0.45f, 0.35f);
}

void ProgressPanel::showFill(float from, float to) {
    const auto full = m_track->getContentSize();
    const float a = std::clamp(from, 0.f, 1.f), b = std::clamp(to, a, 1.f);
    // GD's fill: the bar's own texture, cut to the part shown.
    m_fill->setTextureRect({full.width * a, 0.f, full.width * (b - a), full.height});
    m_fill->setPosition({12.f + m_track->getScaledContentSize().width * a, 15.f});
    m_fill->setVisible(b > a);
}

void ProgressPanel::onStop(std::function<void()> stop) {
    m_stop = std::move(stop);
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu);
    auto* b = CCMenuItemSpriteExtra::create(ButtonSprite::create("Stop", "bigFont.fnt", "GJ_button_06.png", 0.35f), this,
                                            menu_selector(ProgressPanel::stopClicked));
    b->setPosition({m_width - 36.f, 4.f});
    menu->addChild(b);
}

void ProgressPanel::stopClicked(CCObject*) {
    if (m_stop) m_stop();
}

void ProgressPanel::update(float dt) {
    if (!m_unknown) return;
    // A short segment sweeping the track.
    constexpr float seg = 0.18f;
    m_phase = std::fmod(m_phase + dt * 0.6f, 1.f);
    const float x = (1.f + seg) * m_phase - seg;
    showFill(x, x + seg);
}

} // namespace kp
