#pragma once
#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace kp {

// A job's progress at the top of the screen: title, status, bar and time left.
class ProgressPanel : public cocos2d::CCNode {
public:
    static ProgressPanel* create(const std::string& title);
    // fraction < 0: unknown, the bar just moves.
    void set(const std::string& status, double fraction, double elapsed, const std::string& detail);
    // Shows a Stop button that calls `stop`.
    void onStop(std::function<void()> stop);

private:
    bool init(const std::string& title);
    void update(float dt) override;

    cocos2d::CCLabelBMFont *m_status = nullptr, *m_right = nullptr, *m_detail = nullptr;
    cocos2d::CCLayerColor *m_track = nullptr, *m_fill = nullptr;
    float m_width = 0, m_phase = 0;
    bool m_unknown = true;
    std::function<void()> m_stop;
    void stopClicked(cocos2d::CCObject*);
};

} // namespace kp
