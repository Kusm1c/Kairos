#include "WindowFile.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace kpsim::search {

const char* const kWidthBuckets[6] = {"1", "2-3", "4-6", "7-12", "13-24", "25+"};

int widthBucket(int w) {
    if (w <= 1) return 0;
    if (w <= 3) return 1;
    if (w <= 6) return 2;
    if (w <= 12) return 3;
    if (w <= 24) return 4;
    return 5;
}

namespace {
double phi(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }
} // namespace

WindowStats windowStats(const WindowReport& r, double sigmaMs) {
    WindowStats s;
    s.edges = r.edges.size();
    for (auto& v : r.removable)
        for (bool b : v) s.removable += b;
    if (r.edges.empty()) return s;
    std::vector<int> widths;
    const double sigma = std::max(1e-6, sigmaMs * 240.0 / 1000.0);   // in ticks
    double chance = 1.0;
    for (const auto& w : r.edges) {
        int width = w.early + w.late + 1;
        widths.push_back(width);
        s.framePerfect += width == 1;
        ++s.histogram[widthBucket(width)];
        // Off by k ticks with k the rounded error: fine while -early <= k <= late.
        chance *= phi((w.late + 0.5) / sigma) - phi(-(w.early + 0.5) / sigma);
    }
    std::sort(widths.begin(), widths.end());
    s.narrowest = widths.front();
    s.medianWidth = widths[widths.size() / 2];
    s.humanChance = chance;
    return s;
}

std::string writeWindowFile(const WindowReport& r, const std::string& macro, uint64_t plan) {
    std::ostringstream o;
    o << "kairos-windows 1\n";
    o << "plan " << plan << "\n";
    o << "macro " << macro << "\n";
    o << "base " << (int)r.base.outcome << " " << r.base.tick << "\n";
    o << "trials " << r.trials << " " << r.pruned << " " << (r.complete ? 1 : 0) << "\n";
    if (r.cap) o << "cap " << r.cap << "\n";
    for (const auto& w : r.edges)
        o << "edge " << w.player << " " << w.hold << " " << (w.press ? 1 : 0) << " " << w.tick << " " << w.early << " "
          << w.late << " " << (w.earlyLimited ? 1 : 0) << " " << (w.lateLimited ? 1 : 0) << "\n";
    for (int p = 0; p < 2; ++p)
        for (size_t i = 0; i < r.removable[p].size(); ++i)
            if (r.removable[p][i]) o << "removable " << p << " " << i << "\n";
    return o.str();
}

bool readWindowFile(const std::string& text, WindowReport& r, std::string& macro, uint64_t& plan) {
    r = WindowReport{};
    plan = 0;
    std::istringstream in(text);
    std::string line, tag;
    if (!std::getline(in, line) || line.rfind("kairos-windows", 0) != 0) return false;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        l >> tag;
        if (tag == "plan") {
            l >> plan;
        } else if (tag == "macro") {
            std::getline(l >> std::ws, macro);
        } else if (tag == "base") {
            int o;
            l >> o >> r.base.tick;
            r.base.outcome = (Outcome)o;
        } else if (tag == "trials") {
            int complete;
            l >> r.trials >> r.pruned >> complete;
            r.complete = complete;
        } else if (tag == "cap") {
            l >> r.cap;
        } else if (tag == "edge") {
            EdgeWindow w;
            int press, el, ll;
            l >> w.player >> w.hold >> press >> w.tick >> w.early >> w.late >> el >> ll;
            w.press = press;
            w.earlyLimited = el;
            w.lateLimited = ll;
            r.edges.push_back(w);
        } else if (tag == "removable") {
            int p;
            size_t i;
            l >> p >> i;
            if (p < 0 || p > 1) continue;
            if (r.removable[p].size() <= i) r.removable[p].resize(i + 1, false);
            r.removable[p][i] = true;
        }
    }
    return true;
}

} // namespace kpsim::search
