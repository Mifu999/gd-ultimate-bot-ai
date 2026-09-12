#include "HUD.hpp"

#include "../game/BotRuntime.hpp"

using namespace geode::prelude;

namespace gdubai {

HUD* HUD::create() {
    auto* node = new HUD();
    if (node && node->init()) {
        node->autorelease();
        return node;
    }
    CC_SAFE_DELETE(node);
    return nullptr;
}

HUD* HUD::attach(CCNode* parent) {
    if (!parent) return nullptr;
    if (auto* existing = typeinfo_cast<HUD*>(parent->getChildByID("gdubai-hud"))) {
        return existing;
    }
    auto* hud = HUD::create();
    if (!hud) return nullptr;
    hud->setID("gdubai-hud");
    parent->addChild(hud, 9000);
    return hud;
}

bool HUD::init() {
    if (!CCNode::init()) return false;

    auto const winSize = CCDirector::sharedDirector()->getWinSize();

    m_status = CCLabelBMFont::create("", "bigFont.fnt");
    m_status->setAnchorPoint({0.f, 1.f});
    m_status->setPosition({6.f, winSize.height - 6.f});
    m_status->setScale(0.35f);
    m_status->setID("status");
    addChild(m_status);

    m_detail = CCLabelBMFont::create("", "chatFont.fnt");
    m_detail->setAnchorPoint({0.f, 1.f});
    m_detail->setPosition({6.f, winSize.height - 26.f});
    m_detail->setScale(0.42f);
    m_detail->setOpacity(180);
    m_detail->setID("detail");
    addChild(m_detail);

    schedule(schedule_selector(HUD::onTick), 0.15f);
    refresh();
    return true;
}

void HUD::onTick(float) {
    // The offline solver finishes on a worker thread. poll() is the handoff
    // point onto the main thread; it is cheap and returns immediately when
    // there is nothing to collect.
    OfflineSolver::get().poll();
    refresh();
}

void HUD::refresh() {
    auto& runtime = BotRuntime::get();
    bool const running = runtime.running();

    if (m_status) {
        m_status->setVisible(running);
        if (running) m_status->setString(runtime.director().statusLine().c_str());
    }
    if (m_detail) {
        m_detail->setVisible(running);
        if (running) m_detail->setString(runtime.director().detailLine().c_str());
    }
}

}  // namespace gdubai
