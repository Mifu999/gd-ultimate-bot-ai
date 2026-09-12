#pragma once

#include "Types.hpp"

#include <matjson.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gdubai {

// The working macro: the append-only, rollback-capable list of inputs the
// director is building. Frames are absolute on the 240 TPS grid.
//
// Invariant maintained by every mutator: `events` is sorted by frame, and the
// (button, player) state implied by replaying it is well defined - i.e. we
// never emit two consecutive `down` for the same button without a release in
// between. truncate() restores that invariant by re-deriving the held state.
class Macro {
public:
    std::string levelKey;
    std::string levelName;
    std::uint32_t levelId = 0;
    std::uint32_t tps = static_cast<std::uint32_t>(kTPS);

    // GJBaseGameLayer::m_randomSeed at the moment the run started, truncated to
    // 32 bits for GDR's `seed` field.
    //
    // This is the one lever that can make a macro reproduce on an RNG level:
    // the seed is redrawn every attempt, so a replayer that restores it (xdBot
    // and pekoBot both ship a seed modifier) can reproduce a run that would
    // otherwise diverge. Recorded unconditionally; whether a given replayer
    // honours it is out of our hands.
    std::int32_t seed = 0;
    Source dominantSource = Source::Unknown;
    bool certified = false;   // replayed end-to-end in normal mode successfully
    double reachedPercent = 0.0;

    std::vector<InputEvent> const& events() const { return m_events; }
    bool empty() const { return m_events.empty(); }
    std::size_t size() const { return m_events.size(); }

    Frame lastFrame() const {
        return m_events.empty() ? 0 : m_events.back().frame;
    }

    // Appends a committed stretch. Events must already be sorted and must not
    // start before the current last frame. Redundant transitions (pressing a
    // button already held) are dropped rather than written, which is what keeps
    // the exported macro clean enough for other replay mods to consume.
    void append(std::vector<InputEvent> const& incoming);

    // Rollback primitive: keep only the first `count` events. Used when the
    // director drops a checkpoint and has to rewind the macro with it.
    void truncate(std::size_t count);

    // Drops every event at or after `frame`. Returns how many were removed.
    std::size_t truncateFromFrame(Frame frame);

    void clear();

    // Button state that replaying the whole macro would leave you in.
    bool held(int button, bool player2) const;

    // --- persistence -----------------------------------------------------

    matjson::Value toJson() const;
    static std::optional<Macro> fromJson(matjson::Value const& value);

    // GDReplayFormat v2 blob, readable by xdBot / Eclipse / MegaHack.
    std::vector<std::uint8_t> toGdr2() const;

private:
    std::vector<InputEvent> m_events;
    // held[player][button]; index 0 of button is unused so the 1..3 ids map
    // straight through with no arithmetic at the call sites.
    bool m_held[2][4] = {};

    void rebuildHeldState();
};

}  // namespace gdubai
