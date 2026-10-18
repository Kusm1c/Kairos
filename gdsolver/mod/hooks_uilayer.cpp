#include "mod/playlayer_helpers.hpp"
#include <Geode/modify/UILayer.hpp>

class $modify(UILayer) {
    void handleKeypress(cocos2d::enumKeyCodes key, bool down, double timestamp) {
        if (key == cocos2d::enumKeyCodes::KEY_X && takeover::personDriving()) {
            static bool s_down = false;
            if (down && !s_down) takeover::removeByKey(PlayLayer::get());
            s_down = down;
            return;
        }
        UILayer::handleKeypress(key, down, timestamp);
    }

    // ...and the on-screen delete button, which is onDeleteCheck's (0x4ce0e0).
    void onDeleteCheck(cocos2d::CCObject* sender) {
        if (takeover::personDriving()) {
            takeover::removeByKey(PlayLayer::get());
            return;
        }
        UILayer::onDeleteCheck(sender);
    }
};
