#pragma once

#include "../core/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

class CheckpointObject;
class PlayLayer;

namespace gdubai {

// One anchor the director can fall back to.
//
// `checkpoint` is null for the implicit spawn anchor at frame 0, which always
// sits at the bottom of the stack and is never popped.
struct Anchor {
    CheckpointObject* checkpoint = nullptr;
    AnchorState state;
    std::size_t macroLength = 0;   // macro event count at the moment it was placed
    Frame frame = 0;

    // Button state at the instant the checkpoint was taken.
    //
    // This matters because PlayerCheckpoint does NOT store whether a button is
    // held - its ~110 saved fields cover position, velocity, gamemode, gravity,
    // slopes and collisions, but nothing about input. On respawn the player
    // comes back with every button released. If the solved segment was mid-hold
    // when the checkpoint landed, the replay silently loses that hold.
    //
    // (The same bug is visible in xdBot's changelog: "Fixed macro not recording
    // a release when you place a checkpoint while holding.")
    //
    // We therefore record it here and re-apply it right after loadFromCheckpoint.
    bool held[2][4] = {};

    // Bookkeeping the arbiter uses to escalate.
    std::uint32_t failures = 0;      // attempts that died before committing
    std::uint32_t horizonBoost = 0;  // extra lookahead granted after rollbacks
    bool simDistrusted = false;      // gd-sim disagreed with reality here
    bool fromMemory = false;         // the segment leaving here came from memory
};

// A LIFO stack of anchors mirroring PlayLayer's own m_checkpointArray.
//
// The two must stay in lockstep: every push corresponds to one markCheckpoint()
// and every pop to one removeCheckpoint(false). verifyAgainst() re-checks that
// invariant against the game, because a desync here would make the director
// truncate the macro at the wrong offset - a silent, very hard to debug failure.
class CheckpointLedger {
public:
    void reset(AnchorState const& spawnState);

    void push(Anchor anchor);
    bool pop();                 // never pops the spawn anchor; false if refused

    Anchor& top();
    Anchor const& top() const;
    std::size_t depth() const { return m_anchors.size(); }
    bool atSpawn() const { return m_anchors.size() <= 1; }

    std::vector<Anchor> const& all() const { return m_anchors; }

    // Compares depth with PlayLayer::m_checkpointArray (+1 for the spawn anchor).
    // Returns true when they agree. On disagreement it logs both counts; the
    // caller is expected to abort the run rather than carry on from a bad state.
    bool verifyAgainst(PlayLayer* layer) const;

    std::string describeTop() const;

private:
    std::vector<Anchor> m_anchors;
};

}  // namespace gdubai
