#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>
#include <string>

namespace kp {

// A label at `scale`, shrunk to `maxWidth` but never under `minScale`. Past that the text is cut
// with "...": a longer text belongs in a details view, not in a smaller font.
inline void fitLabel(cocos2d::CCLabelBMFont* l, float maxWidth, float scale, float minScale = 0.45f) {
    l->setScale(scale);
    const float w = l->getContentSize().width;
    if (w <= 0.f || w * scale <= maxWidth) return;
    const float s = std::max(minScale, maxWidth / w);
    l->setScale(s);
    std::string text = l->getString();
    while (l->getContentSize().width * s > maxWidth && text.size() > 4) {
        text.erase(text.size() - 4);   // a character, then the "..." added last time
        text += "...";
        l->setString(text.c_str());
    }
}

// `text` broken at spaces into lines at most `maxWidth` wide at `scale` (explicit newlines kept).
inline std::string wrapText(const std::string& text, const char* font, float scale, float maxWidth) {
    auto* probe = cocos2d::CCLabelBMFont::create("", font);
    auto width = [&](const std::string& s) {
        probe->setString(s.c_str());
        return probe->getContentSize().width * scale;
    };
    std::string out, line, word;
    auto flushWord = [&] {
        if (word.empty()) return;
        const std::string joined = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(joined) > maxWidth) {
            out += line + "\n";
            line = word;
        } else {
            line = joined;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') {
            flushWord();
        } else if (c == '\n') {
            flushWord();
            out += line + "\n";
            line.clear();
        } else {
            word += c;
        }
    }
    flushWord();
    return out + line;
}

} // namespace kp
