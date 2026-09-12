#include "BotRuntime.hpp"
#include "FrameClock.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using namespace gdubai;

// Every method hooked here is addressed on win/imac/m1/ios in the 2.2081
// bindings, so none of these is at risk of the "hooked an inlined function"
// failure mode. Checked with:
//   broma.py PlayLayer <method>   => hookable on: imac, ios, m1, win
//
// Deliberately NOT hooked: getLastCheckpoint, loadLastCheckpoint,
// queueCheckpoint, checkpointWithID, getEndPosition - all `win inline`. We call
// them (the codegen emits reconstructed bodies) but hooking them would fail to
// compile on Windows.

struct GDUBAIGameLayer : geode::Modify<GDUBAIGameLayer, GJBaseGameLayer> {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto* play = typeinfo_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this));

        if (play && BotRuntime::get().running()) {
            // Feed this frame's inputs BEFORE the game consumes them, which is
            // what makes them land on the frame we intended rather than the
            // next one.
            BotRuntime::get().director().onProcessCommands(play);
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (play && BotRuntime::get().running()) {
            FrameClock::get().tick();
        }
    }
};

struct GDUBAIPlayLayer : geode::Modify<GDUBAIPlayLayer, PlayLayer> {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& runtime = BotRuntime::get();
        if (!runtime.running()) return;

        runtime.director().onPostUpdate(this);

        // Cheap correctness alarm: our integer counter and GD's own restored
        // clock should never drift by more than a frame or two. If they do, the
        // macro is being written against the wrong timeline.
        double const drift = FrameClock::get().drift(this);
        if (std::abs(drift) > 4.0) {
            static int warned = 0;
            if (warned++ < 5) {
                log::warn("GDUBAI: frame clock drift {:.1f} frames vs m_levelTime", drift);
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& runtime = BotRuntime::get();

        // m_anticheatSpike deaths are GD's own anti-cheat probe, not a real
        // death - NEATGD handles this the same way and it matters, because
        // counting it would poison the death histogram.
        bool const anticheat = object == m_anticheatSpike;

        if (!anticheat && runtime.running() && player == m_player1) {
            runtime.director().onPlayerDeath(this, player);
        }

        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        auto& runtime = BotRuntime::get();
        if (runtime.running()) {
            runtime.director().onLevelReset(this);
        }
    }

    void levelComplete() {
        auto& runtime = BotRuntime::get();
        if (runtime.running()) {
            runtime.director().onLevelComplete(this);
            // Suppress the vanilla end screen while the director is still
            // working; it would tear down the layer under us.
            if (runtime.running()) return;
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        auto& runtime = BotRuntime::get();
        if (runtime.running()) {
            runtime.director().onQuit(this);
        }
        PlayLayer::onQuit();
    }
};
