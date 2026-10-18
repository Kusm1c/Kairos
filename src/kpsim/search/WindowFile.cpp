#include "WindowFile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace kpsim::search {

const char* const kWidthBuckets[kWidthBucketCount] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11+"};

int widthBucket(int w) { return std::clamp(w, 1, kWidthBucketCount) - 1; }

namespace {

double phi(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }

// nandl.pages.dev base model: error N(0, 1/L s) at the window centre, over 10 ticks always fine.
double nandlPrecision(std::vector<std::pair<double, int>> inputs) {   // (seconds, width)
    std::sort(inputs.begin(), inputs.end());
    if (std::none_of(inputs.begin(), inputs.end(), [](auto& i) { return i.second <= 10; })) return 0;
    auto clearTime = [&](double L) {
        double attempt = 0, reach = 1;
        for (auto [t, width] : inputs) {
            const double p = width > 10 ? 1.0 : std::erf(width / 2.0 * L / 240.0 / std::sqrt(2.0));
            attempt += t * reach * (1 - p);
            reach *= p;
        }
        attempt += inputs.back().first * reach;
        return reach > 0 ? attempt / reach : INFINITY;
    };
    constexpr double kDay = 86400;
    double lo = 0, hi = 100;
    while (clearTime(hi) > kDay && hi < 1e9) hi *= 2;
    for (int i = 0; i < 100; ++i) {
        const double mid = (lo + hi) / 2;
        (clearTime(mid) > kDay ? lo : hi) = mid;
    }
    return hi;
}

} // namespace

WindowStats windowStats(const WindowReport& r, double sigmaMs) {
    WindowStats s;
    s.edges = r.edges.size();
    for (auto& v : r.removable)
        for (bool b : v) s.removable += b;
    if (r.edges.empty()) return s;
    std::vector<int> widths;
    std::vector<std::pair<double, int>> timed;
    const double sigma = std::max(1e-6, sigmaMs * 240.0 / 1000.0);   // in ticks
    double chance = 1.0;
    for (const auto& w : r.edges) {
        int width = w.early + w.late + 1;
        widths.push_back(width);
        timed.push_back({w.tick / 240.0, width});
        s.framePerfect += width == 1;
        // A single frame (k ticks) fits: always up to k, below 2k for some phases.
        for (int f = 0; f < 3; ++f) {
            const int k = 4 >> f;
            s.perfectAt[f] += width <= k ? 1.0 : width < 2 * k ? (double)(2 * k - width) / k : 0.0;
        }
        ++s.histogram[widthBucket(width)];
        // Off by k ticks with k the rounded error: fine while -early <= k <= late.
        chance *= phi((w.late + 0.5) / sigma) - phi(-(w.early + 0.5) / sigma);
    }
    std::sort(widths.begin(), widths.end());
    s.narrowest = widths.front();
    s.medianWidth = widths[widths.size() / 2];
    s.humanChance = chance;
    s.precision = nandlPrecision(std::move(timed));
    return s;
}

std::string writeWindowFile(const WindowReport& r, const std::string& macro, uint64_t plan) {
    std::ostringstream o;
    o << "kairos-windows 2\n";
    o << "plan " << plan << "\n";
    o << "macro " << macro << "\n";
    o << "base " << (int)r.base.outcome << " " << r.base.tick << "\n";
    o << "trials " << r.trials << " " << r.pruned << " " << (r.complete ? 1 : 0) << "\n";
    if (r.cap) o << "cap " << r.cap << "\n";
    o << "followed " << r.followed << "\n";
    for (const auto& w : r.edges)
        o << "edge " << w.player << " " << w.hold << " " << (w.press ? 1 : 0) << " " << w.tick << " " << w.early << " "
          << w.late << " " << (w.earlyLimited ? 1 : 0) << " " << (w.lateLimited ? 1 : 0) << "\n";
    for (int p = 0; p < 2; ++p)
        for (size_t i = 0; i < r.removable[p].size(); ++i)
            if (r.removable[p][i]) o << "removable " << p << " " << i << "\n";
    for (const auto& t : r.tests) {
        const auto& w = t.edge;
        o << "check " << w.player << " " << w.hold << " " << (w.press ? 1 : 0) << " " << w.tick << " " << w.early << " "
          << w.late << " " << w.ground << "\n";
        for (const auto& x : t.tests) {
            o << "test " << x.shift << " " << (x.passed ? 1 : 0) << " " << x.death << " ";
            for (int p = 0; p < 2; ++p) {
                if (p) o << "|";
                for (const Hold& h : x.plan.channel(p)) {
                    o << h.press << ":";
                    if (h.release == macro::kNever) o << "n";
                    else o << h.release;
                    o << ",";
                }
            }
            o << "\n";
        }
    }
    return o.str();
}

bool readWindowFile(const std::string& text, WindowReport& r, std::string& macro, uint64_t& plan) {
    r = WindowReport{};
    plan = 0;
    std::istringstream in(text);
    std::string line, tag;
    if (!std::getline(in, line) || line.rfind("kairos-windows 2", 0) != 0) return false;
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
        } else if (tag == "followed") {
            l >> r.followed;
        } else if (tag == "edge") {
            EdgeWindow w;
            int press, el, ll;
            l >> w.player >> w.hold >> press >> w.tick >> w.early >> w.late >> el >> ll;
            w.press = press;
            w.earlyLimited = el;
            w.lateLimited = ll;
            r.edges.push_back(w);
        } else if (tag == "check") {
            EdgeTests t;
            int press;
            l >> t.edge.player >> t.edge.hold >> press >> t.edge.tick >> t.edge.early >> t.edge.late >> t.edge.ground;
            t.edge.press = press;
            r.tests.push_back(std::move(t));
        } else if (tag == "test" && !r.tests.empty()) {
            EdgeTest x;
            int passed;
            std::string holds;
            l >> x.shift >> passed >> x.death >> holds;
            x.passed = passed;
            int p = 0;
            for (size_t i = 0; i < holds.size();) {
                if (holds[i] == '|') { ++p; ++i; continue; }
                const size_t colon = holds.find(':', i), comma = holds.find(',', i);
                if (colon == std::string::npos || comma == std::string::npos || p > 1) break;
                Hold h;
                h.press = (Tick)std::strtoul(holds.c_str() + i, nullptr, 10);
                const std::string rel = holds.substr(colon + 1, comma - colon - 1);
                h.release = rel == "n" ? macro::kNever : (Tick)std::strtoul(rel.c_str(), nullptr, 10);
                x.plan.channel(p).push_back(h);
                i = comma + 1;
            }
            r.tests.back().tests.push_back(std::move(x));
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
