#include "HUD.hpp"

#include "../game/BotRuntime.hpp"
#include "../game/OfflineSolver.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using namespace gdubai;

namespace {

// The pause menu's own right-hand column, provided by geode.node-ids. Falling
// back to a fresh menu keeps the button reachable if node IDs ever change.
CCMenu* resolvePauseMenu(CCNode* layer) {
    if (auto* menu = typeinfo_cast<CCMenu*>(layer->getChildByID("right-button-menu"))) {
        return menu;
    }
    auto const winSize = CCDirector::sharedDirector()->getWinSize();
    auto* menu = CCMenu::create();
    menu->setID("gdubai-pause-menu");
    menu->setPosition({winSize.width - 50.f, winSize.height - 55.f});
    layer->addChild(menu, 20);
    return menu;
}

CCMenuItemSpriteExtra* makeIconButton(
    char const* id, std::function<void(CCObject*)> callback, float scale = 0.7f) {
    auto* sprite = CCSprite::create("gdubai.png"_spr);
    if (!sprite) {
        // Resource missing (unpacked install, texture pack). A circle button
        // with a letter still works and is better than an invisible button.
        sprite = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
    }
    auto* based = CircleButtonSprite::create(sprite, CircleBaseColor::Green);
    based->setScale(scale);

    auto* item = CCMenuItemExt::createSpriteExtra(
        based, [callback](CCObject* sender) { callback(sender); });
    item->setID(id);
    return item;
}

}  // namespace

// ---------------------------------------------------------------------------

// Geode v5 changed Popup: it is no longer templated, and Popup::init(w, h)
// replaces initAnchored(). See docs.geode-sdk.org/tutorials/migrate-v5,
// "Changes to Popup". The derived init() shadows the base overloads, which is
// the pattern the migration guide itself shows.
class GDUBAIPopup : public geode::Popup {
public:
    static GDUBAIPopup* create() {
        auto* popup = new GDUBAIPopup();
        if (popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }

protected:
    CCLabelBMFont* m_status = nullptr;

    bool init() {
        if (!Popup::init(380.f, 260.f)) return false;

        this->setTitle("GD Ultimate Bot AI");

        // m_size is Popup's own protected size member - more reliable than
        // reaching through m_mainLayer for the content size.
        auto const size = m_size;

        m_status = CCLabelBMFont::create("", "chatFont.fnt");
        m_status->setScale(0.5f);
        m_status->setPosition({size.width / 2.f, size.height / 2.f + 34.f});
        m_status->setAlignment(kCCTextAlignmentCenter);
        m_status->setID("status");
        m_mainLayer->addChild(m_status);

        auto* menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, size.height / 2.f - 14.f});
        menu->setID("actions");
        m_mainLayer->addChild(menu);

        auto addButton = [&](char const* label, char const* texture, CCPoint offset,
                             std::function<void()> action) {
            auto* sprite = ButtonSprite::create(label, "bigFont.fnt", texture);
            sprite->setScale(0.6f);
            auto* item = CCMenuItemExt::createSpriteExtra(
                sprite, [action](CCObject*) { action(); });
            item->setPosition(offset);
            menu->addChild(item);
            return item;
        };

        addButton("Practice run", "GJ_button_01.png", {-72.f, 26.f}, [this] {
            auto* play = PlayLayer::get();
            if (!BotRuntime::get().startPractice(play)) {
                refreshStatus();
                return;
            }
            if (play) HUD::attach(play);
            refreshStatus();
        });

        addButton("Solve offline", "GJ_button_02.png", {72.f, 26.f}, [this] {
            OfflineSolver::get().start(PlayLayer::get());
            refreshStatus();
        });

        addButton("Stop", "GJ_button_06.png", {-72.f, -18.f}, [this] {
            BotRuntime::get().stop("stopped from the popup");
            OfflineSolver::get().stop();
            refreshStatus();
        });

        addButton("Export .gdr2", "GJ_button_01.png", {72.f, -18.f}, [this] {
            BotRuntime::get().exportMacro("gdr2");
            refreshStatus();
        });

        addButton("Export .json", "GJ_button_01.png", {-72.f, -58.f}, [this] {
            BotRuntime::get().exportMacro("json");
            refreshStatus();
        });

        addButton("Settings", "GJ_button_05.png", {72.f, -58.f}, [] {
            geode::openSettingsPopup(Mod::get());
        });

        this->schedule(schedule_selector(GDUBAIPopup::tick), 0.25f);
        refreshStatus();
        return true;
    }

    void tick(float) { refreshStatus(); }

    void refreshStatus() {
        if (!m_status) return;

        auto& runtime = BotRuntime::get();
        std::string text;

        if (runtime.running()) {
            text = runtime.director().statusLine() + "\n" + runtime.director().detailLine();
        } else if (OfflineSolver::get().busy()) {
            text = OfflineSolver::get().statusLine();
        } else {
            text = runtime.lastMessage().empty() ? "Idle." : runtime.lastMessage();
        }

        auto const& macro = runtime.director().macro();
        if (!macro.empty()) {
            text += fmt::format("\nMacro: {} input(s), {:.2f}%{}", macro.size(),
                                macro.reachedPercent,
                                macro.certified ? " - CERTIFIED" : " - not validated");
        }
        m_status->setString(text.c_str());
    }

};

// ---------------------------------------------------------------------------

struct GDUBAIPauseLayer : geode::Modify<GDUBAIPauseLayer, PauseLayer> {
    void customSetup() {
        PauseLayer::customSetup();

        auto* menu = resolvePauseMenu(this);
        if (!menu) return;

        // getChildByID on the resolved menu, not getChildByIDRecursive on the
        // layer: the recursive variant is a Geode cocos extension this build was
        // not verified against, and the button can only live on this menu.
        if (menu->getChildByID("gdubai-button")) return;

        menu->addChild(makeIconButton("gdubai-button", [](CCObject*) {
            if (auto* popup = GDUBAIPopup::create()) popup->show();
        }, 0.62f));
        menu->updateLayout();
    }
};

struct GDUBAILevelInfoLayer : geode::Modify<GDUBAILevelInfoLayer, LevelInfoLayer> {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        auto* menu = typeinfo_cast<CCMenu*>(getChildByID("other-menu"));
        if (!menu) return true;

        menu->addChild(makeIconButton("gdubai-button", [](CCObject*) {
            if (auto* popup = GDUBAIPopup::create()) popup->show();
        }, 0.7f));
        menu->updateLayout();
        return true;
    }
};

struct GDUBAIHudPlayLayer : geode::Modify<GDUBAIHudPlayLayer, PlayLayer> {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (Mod::get()->getSettingValue<bool>("show-hud")) {
            HUD::attach(this);
        }
        return true;
    }
};
