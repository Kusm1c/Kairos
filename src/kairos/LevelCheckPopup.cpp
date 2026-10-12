#include "LevelCheckPopup.hpp"
#include "UiText.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>

using namespace geode::prelude;
using levelwarn::Note;

namespace kp {

ccColor3B severityColor(levelwarn::Severity s) {
    switch (s) {
        case levelwarn::Severity::Bad: return {255, 100, 100};
        case levelwarn::Severity::Caution: return {255, 215, 70};
        case levelwarn::Severity::Info: return {160, 205, 255};
        default: return {120, 230, 130};
    }
}

namespace {

class LevelCheckPopup : public geode::Popup {
public:
    static LevelCheckPopup* create(const std::string& level, std::vector<Note> notes) {
        auto* p = new LevelCheckPopup();
        if (p->init(level, std::move(notes))) {
            p->autorelease();
            return p;
        }
        delete p;
        return nullptr;
    }

private:
    static constexpr float kW = 360.f, kH = 240.f;
    static constexpr float kTitleScale = 0.38f, kDetailScale = 0.6f, kIndent = 20.f;

    bool init(const std::string& level, std::vector<Note> notes) {
        if (!Popup::init(kW, kH)) return false;
        this->setTitle("Level check");
        auto* name = CCLabelBMFont::create(level.c_str(), "goldFont.fnt");
        fitLabel(name, kW - 60.f, 0.5f, 0.35f);
        m_mainLayer->addChildAtPosition(name, Anchor::Top, ccp(0.f, -44.f));

        std::stable_sort(notes.begin(), notes.end(),
                         [](const Note& a, const Note& b) { return a.severity < b.severity; });
        const CCSize list{kW - 30.f, kH - 76.f};
        if (notes.empty()) {
            auto* none = CCLabelBMFont::create("Nothing to report: the solver models\neverything this level uses.",
                                               "chatFont.fnt", list.width / kDetailScale, kCCTextAlignmentCenter);
            none->setScale(kDetailScale);
            m_mainLayer->addChildAtPosition(none, Anchor::Center, ccp(0.f, -14.f));
            return true;
        }

        // Rows first, to know how tall the list is.
        struct Row {
            CCNode* node;
            float height;
        };
        std::vector<Row> rows;
        const float textW = list.width - kIndent - 6.f;
        for (const auto& n : notes) {
            auto* row = CCNode::create();
            auto* title = CCLabelBMFont::create(n.title.c_str(), "bigFont.fnt");
            fitLabel(title, textW, kTitleScale, 0.3f);
            title->setColor(severityColor(n.severity));
            title->setAnchorPoint({0.f, 1.f});
            auto* detail = CCLabelBMFont::create(n.detail.c_str(), "chatFont.fnt", textW / kDetailScale,
                                                 kCCTextAlignmentLeft);
            detail->setScale(kDetailScale);
            detail->setAnchorPoint({0.f, 1.f});
            detail->setColor({225, 225, 225});
            const float titleH = title->getScaledContentSize().height, detailH = detail->getScaledContentSize().height;
            const float h = titleH + 2.f + detailH + 10.f;
            title->setPosition({kIndent, h});
            detail->setPosition({kIndent, h - titleH - 2.f});
            auto* dot = CCDrawNode::create();
            const auto c = severityColor(n.severity);
            dot->drawDot({kIndent / 2.f, h - titleH / 2.f}, 3.5f, {c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f});
            row->addChild(dot);
            row->addChild(title);
            row->addChild(detail);
            rows.push_back({row, h});
        }
        float total = 0.f;
        for (const auto& r : rows) total += r.height;

        auto* scroll = ScrollLayer::create(list);
        const float contentH = std::max(list.height, total + 4.f);
        scroll->m_contentLayer->setContentSize({list.width, contentH});
        float y = contentH - 2.f;
        for (const auto& r : rows) {
            y -= r.height;
            r.node->setPosition({0.f, y});
            scroll->m_contentLayer->addChild(r.node);
        }
        m_mainLayer->addChildAtPosition(scroll, Anchor::Bottom, ccp(-list.width / 2.f, 14.f));
        scroll->scrollToTop();
        return true;
    }
};

} // namespace

void showLevelCheck(const std::string& levelName, std::vector<Note> notes) {
    if (auto* p = LevelCheckPopup::create(levelName, std::move(notes))) p->show();
}

} // namespace kp
