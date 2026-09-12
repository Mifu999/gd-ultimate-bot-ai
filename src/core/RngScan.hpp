#pragma once

// Randomness detection.
//
// Geometry Dash keeps ONE random seed per level. It is regenerated on every
// attempt, and - crucially for this mod - it is preserved when you respawn from
// a practice checkpoint.
//
//   Source: "RNG", Library of Geometria (A-Zalt)
//           gdknowledge/resources/rng.html
//           "There's 1 random seed per level that is generated randomly on each
//            attempt (the seed stays the same when restarting from checkpoint)."
//
// That single sentence decides two things about this mod:
//
//   1. The practice-mode director is SOUND. Rolling back to a checkpoint and
//      re-solving does not reshuffle the level underneath it, so a segment that
//      worked once will behave the same way on the next rollback within the
//      same attempt.
//
//   2. A finished macro is NOT guaranteed to reproduce on a fresh attempt IF
//      the level consumes the seed, because a full restart draws a new one.
//      No amount of cleverness on our side fixes that - it is a property of the
//      game. The only honest response is to detect it and say so.
//
// Four triggers consume the seed (same source): Random, Advanced Random,
// Advanced Follow and Edit Advanced Follow. Their object IDs come from the
// gd-info-explorer object table by FlowVix.

#include <string>
#include <vector>

namespace gdubai {

struct RngTriggerInfo {
    int objectId = 0;
    char const* name = "";
    int count = 0;
};

struct RngReport {
    std::vector<RngTriggerInfo> found;
    int totalCount = 0;

    bool deterministic() const { return totalCount == 0; }

    // One line for the HUD / popup, e.g.
    //   "12 RNG triggers (Random x9, AdvRand x3) - macro may not reproduce"
    std::string summary() const;
};

// Scans a DECOMPRESSED level string (the `;`-separated object list) and counts
// the seed-consuming triggers. Cheap: one linear pass, no full object parse.
RngReport scanForRng(std::string const& levelString);

}  // namespace gdubai
