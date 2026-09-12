#pragma once

#include "../core/Types.hpp"

class PlayLayer;

namespace gdubai {

// An integer physics-frame counter.
//
// Why not just `m_attemptTime * 240`? Two reasons.
//
//   1. The GDReplayFormat spec says so outright: "Use a frame counter to store
//      frames. Multiplying time can lead to inaccuracy in frame counting."
//      (github.com/maxnut/GDReplayFormat). A macro whose frames are derived
//      from a double drifts by a frame here and there, and a frame is the
//      difference between clearing a spike and not.
//
//   2. `PlayLayer::m_attemptTime` is not restored by a practice checkpoint.
//      `GJGameState::m_levelTime` is (CheckpointObject stores a whole
//      GJGameState), but it is still a double.
//
// So we count ticks ourselves and, on every respawn, set the counter back to
// the frame recorded in the checkpoint ledger. That gives an exact integer that
// is also consistent across rollbacks.
//
// drift() compares us against the game's own clock; it is only used for a log
// warning, never for control flow.
class FrameClock {
public:
    static FrameClock& get();

    void reset();
    void setFrame(Frame frame) { m_frame = frame; }
    Frame frame() const { return m_frame; }

    // Called once per physics step from GJBaseGameLayer::processCommands.
    void tick() { ++m_frame; }

    // Positive when we are ahead of GJGameState::m_levelTime.
    double drift(PlayLayer* layer) const;

private:
    Frame m_frame = 0;
};

}  // namespace gdubai
