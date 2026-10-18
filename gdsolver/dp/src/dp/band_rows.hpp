#pragma once
#include <sstream>
#include <string>
#include <vector>

namespace dp {
struct BandSample {
    long long t = 0;
    bool written = false;
    float pmin = 0.f, pmax = 0.f;
    int cam = -1;
};

inline std::string bandRows(const std::vector<BandSample>& samples, long long* rowsOut = nullptr) {
    std::ostringstream o;
    float lf = -1e9f, lc = -1e9f;
    char lk = 0;
    int ls = -2;
    long long rows = 0;
    for (const BandSample& s : samples) {
        if (!s.written) continue;
        const char k = (s.pmax > s.pmin) ? 'I' : 'D';
        if (k == lk && s.pmin == lf && s.pmax == lc) {
            if (s.cam != ls) {
                o << s.t << ",S," << s.cam << '\n';
                ls = s.cam;
                ++rows;
            }
            continue;
        }
        lk = k; lf = s.pmin; lc = s.pmax; ls = s.cam;
        if (k == 'I')
            o << s.t << ",I," << s.pmin << ',' << s.pmax << ',' << s.cam << '\n';
        else
            o << s.t << ",D," << s.pmin << ',' << s.pmin << ',' << s.cam << '\n';
        ++rows;
    }
    if (rowsOut) *rowsOut = rows;
    return o.str();
}
}  // namespace dp
