#include "StatsPopup.hpp"
#include "kpsim/search/WindowFile.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace geode::prelude;
using kpsim::search::EdgeWindow;
using kpsim::search::WindowReport;

namespace kp {
namespace {

constexpr size_t kListed = 200;   // tightest edges shown

ccColor3B widthColor(int width) {
    if (width <= 1) return {255, 70, 70};
    if (width <= 3) return {255, 150, 60};
    if (width <= 6) return {255, 220, 80};
    if (width <= 12) return {170, 255, 110};
    return {90, 220, 255};
}

double ms(double ticks) { return ticks * 1000.0 / 240.0; }

class StatsPopup : public geode::Popup {
public:
    static StatsPopup* create(const WindowReport& r, const std::string& macro, std::function<void()> practice) {
        auto* p = new StatsPopup();
        p->m_practice = std::move(practice);
        p->m_report = r;
        if (p->init(r, macro)) {
            p->autorelease();
            return p;
        }
        delete p;
        return nullptr;
    }

private:
    CCLabelBMFont* text(const std::string& s, float scale, CCPoint pos, CCPoint anchor = {0.f, 0.5f}) {
        auto* l = CCLabelBMFont::create(s.c_str(), "chatFont.fnt");
        l->setScale(scale);
        l->setAnchorPoint(anchor);
        l->setPosition(pos);
        m_mainLayer->addChild(l);
        return l;
    }

    bool init(const WindowReport& r, const std::string& macro) {
        if (!Popup::init(440.f, 290.f)) return false;
        this->setTitle("Frame windows");
        const auto size = m_mainLayer->getContentSize();
        const double sigma = (double)Mod::get()->getSettingValue<int64_t>("human-sigma-ms");
        const auto st = kpsim::search::windowStats(r, sigma);
        const bool two = std::any_of(r.edges.begin(), r.edges.end(), [](auto& w) { return w.player == 1; });

        float y = size.height - 48.f;
        const char* outcome = r.base.outcome == kpsim::search::Outcome::Finished ? "finished"
                              : r.base.outcome == kpsim::search::Outcome::Died  ? "died"
                                                                                : "alive";
        text(fmt::format("{}  -  base run {} at {:.2f} s{}", macro, outcome, r.base.tick / 240.0,
                         r.complete ? "" : "  (partial)"),
             0.55f, {16.f, y});
        y -= 16.f;
        text(fmt::format("{} edges   {} frame-perfect   narrowest {} ticks ({:.1f} ms)   median {} ({:.1f} ms)",
                         st.edges, st.framePerfect, st.narrowest, ms(st.narrowest), (int)st.medianWidth,
                         ms(st.medianWidth)),
             0.55f, {16.f, y});
        y -= 16.f;
        auto* chance = text(fmt::format("Human success estimate (timing spread {:.0f} ms): {:.1f}%   {} clicks not needed",
                                        sigma, st.humanChance * 100.0, st.removable),
                            0.55f, {16.f, y});
        chance->setColor(st.humanChance > 0.5 ? ccColor3B{120, 255, 120} : ccColor3B{255, 150, 90});

        // Histogram of window widths.
        const float hx = 22.f, hy = 34.f, hw = 170.f, hh = 130.f;
        text("window width (ticks)", 0.5f, {hx, hy - 20.f});
        size_t top = 1;
        for (size_t c : st.histogram) top = std::max(top, c);
        const float bw = hw / 6.f;
        for (int b = 0; b < 6; ++b) {
            const float h = std::max(1.f, hh * (float)st.histogram[b] / (float)top);
            int sample = b == 0 ? 1 : b == 1 ? 3 : b == 2 ? 6 : b == 3 ? 12 : b == 4 ? 24 : 25;
            auto c = widthColor(sample);
            auto* bar = CCLayerColor::create({c.r, c.g, c.b, 220}, bw - 6.f, h);
            bar->setPosition({hx + b * bw, hy});
            m_mainLayer->addChild(bar);
            text(kpsim::search::kWidthBuckets[b], 0.45f, {hx + b * bw + (bw - 6.f) / 2, hy - 8.f}, {0.5f, 0.5f});
            text(std::to_string(st.histogram[b]), 0.45f, {hx + b * bw + (bw - 6.f) / 2, hy + h + 7.f}, {0.5f, 0.5f});
        }

        buildList();

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(menu);
        auto button = [&](const char* text, SEL_MenuHandler sel, CCPoint pos) {
            auto* b = CCMenuItemSpriteExtra::create(ButtonSprite::create(text, "bigFont.fnt", "GJ_button_04.png", 0.4f),
                                                    this, sel);
            b->setPosition(pos);
            menu->addChild(b);
        };
        button("Sort", menu_selector(StatsPopup::onSort), {size.width - 60.f, 232.f});
        if (m_practice) button("Practice", menu_selector(StatsPopup::onPractice), {size.width - 150.f, 232.f});
        return true;
    }

    void onSort(CCObject*) {
        m_byTime = !m_byTime;
        buildList();
    }

    void onPractice(CCObject*) {
        auto practice = m_practice;
        this->onClose(nullptr);
        if (practice) practice();
    }

    // Tightest edges first, or in the order they come.
    void buildList() {
        const auto size = m_mainLayer->getContentSize();
        const auto& r = m_report;
        const bool two = std::any_of(r.edges.begin(), r.edges.end(), [](auto& w) { return w.player == 1; });
        if (m_scroll) m_scroll->removeFromParent();
        if (m_caption) m_caption->removeFromParent();
        std::vector<EdgeWindow> edges = r.edges;
        if (!m_byTime)
            std::stable_sort(edges.begin(), edges.end(), [](const EdgeWindow& a, const EdgeWindow& b) {
                return a.early + a.late < b.early + b.late;
            });
        if (edges.size() > kListed) edges.resize(kListed);
        const CCSize listSize{220.f, 190.f};
        auto* scroll = ScrollLayer::create(listSize);
        m_scroll = scroll;
        scroll->setPosition({size.width - listSize.width - 14.f, 16.f});
        m_mainLayer->addChild(scroll);
        const float row = 13.f;
        const float contentH = std::max(listSize.height, row * (float)edges.size() + 4.f);
        scroll->m_contentLayer->setContentSize({listSize.width, contentH});
        // At the cap: "20-" (20 or more earlier), "20+" (later).
        auto capped = [&](int v, bool limited) { return r.cap && limited && v >= r.cap; };
        auto side = [&](int v, bool limited, bool early) {
            if (capped(v, limited)) return fmt::format("{}{}", r.cap, early ? "-" : "+");
            return fmt::format("{}{}{}", early ? "-" : "+", v, limited && !r.cap ? "+" : "");
        };
        for (size_t i = 0; i < edges.size(); ++i) {
            const auto& w = edges[i];
            const int width = w.early + w.late + 1;
            const char* more = capped(w.early, w.earlyLimited) || capped(w.late, w.lateLimited) ? "+" : "";
            auto label = fmt::format("{}#{} {}  {:.2f}s   {} / {}   {}{}t {:.0f}{}ms", two ? (w.player ? "P2 " : "P1 ") : "",
                                     w.hold + 1, w.press ? "press" : "release", w.tick / 240.0,
                                     side(w.early, w.earlyLimited, true), side(w.late, w.lateLimited, false), width,
                                     more, ms(width), more);
            auto* l = CCLabelBMFont::create(label.c_str(), "chatFont.fnt");
            l->setScale(0.5f);
            l->setAnchorPoint({0.f, 0.5f});
            l->setColor(widthColor(width));
            l->setPosition({4.f, contentH - row * ((float)i + 0.5f) - 2.f});
            scroll->m_contentLayer->addChild(l);
        }
        scroll->scrollToTop();
        m_caption = text(fmt::format("{} {} of {} edges  {}", m_byTime ? "first" : "tightest", edges.size(), r.edges.size(),
                                     r.cap ? fmt::format("({0}+ / {0}- = {0} or more)", r.cap) : "(+ = range limit)"),
                         0.45f, {size.width - listSize.width - 14.f, 210.f});
    }

    std::function<void()> m_practice;
    WindowReport m_report;
    ScrollLayer* m_scroll = nullptr;
    CCLabelBMFont* m_caption = nullptr;
    bool m_byTime = false;
};

} // namespace

void showWindowStats(const WindowReport& r, const std::string& macro, std::function<void()> practice) {
    if (auto* p = StatsPopup::create(r, macro, std::move(practice))) p->show();
}

bool showWindowStatsFile(const std::filesystem::path& path, std::function<void()> practice) {
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    WindowReport r;
    std::string macro;
    uint64_t plan = 0;
    if (!kpsim::search::readWindowFile(ss.str(), r, macro, plan)) return false;
    showWindowStats(r, macro, std::move(practice));
    return true;
}

} // namespace kp
