#pragma once

#include <Geode/Geode.hpp>

namespace gdubai {

// A two-line overlay pinned to the top-left of PlayLayer showing what the
// director is doing. Refreshed a few times a second rather than every frame:
// at 240 TPS with speedhack on, setString() every tick is measurable.
class HUD : public cocos2d::CCNode {
public:
    static HUD* create();
    static HUD* attach(cocos2d::CCNode* parent);

    void refresh();

protected:
    bool init() override;

private:
    void onTick(float dt);

    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_detail = nullptr;
};

}  // namespace gdubai
