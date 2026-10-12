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

} // namespace kp
