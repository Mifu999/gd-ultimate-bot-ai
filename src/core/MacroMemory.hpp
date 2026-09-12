#pragma once

#include "Macro.hpp"
#include "Types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gdubai {

// Per-level persistent knowledge.
//
// This is the piece that makes a second run cheaper than the first. Three
// separate things are remembered, and they are used at three different moments:
//
//   segments  keyed by the entry AnchorState hash. When the director reaches an
//             anchor it has solved before - same position, velocity, gamemode,
//             gravity - it replays the stored inputs instead of searching. That
//             is the "a part that was already learned is never re-learned" rule.
//
//   deaths    a frame -> count histogram, accumulated across every run ever made
//             on this level. The arbiter reads it to decide where to give the
//             solver a longer horizon and a bigger budget before it even starts
//             failing there.
//
//   best      the best complete-or-partial macro so far, so the mod can offer
//             "continue from where the last run got stuck" instead of restarting
//             at 0% each session.
//
// Storage is one JSON file per level under the mod's save dir. Writes are
// debounced by the caller (see PracticeDirector) because committing a segment
// happens several times a second at high speed.
class MacroMemory {
public:
    static std::filesystem::path directory();
    static std::filesystem::path fileFor(std::string const& levelKey);

    // Never fails: a missing or corrupt file yields an empty, usable memory.
    // `corrupt` is set when a file existed but could not be parsed, so the UI
    // can tell the user rather than silently starting from scratch.
    static MacroMemory load(std::string const& levelKey, bool* corrupt = nullptr);

    bool save() const;

    std::string const& levelKey() const { return m_levelKey; }
    void setLevelKey(std::string key) { m_levelKey = std::move(key); }
    void setLevelName(std::string name) { m_levelName = std::move(name); }

    // --- segments --------------------------------------------------------

    // Returns the stored segment for this entry state, if any. Only segments
    // that were committed AND never subsequently invalidated are returned.
    std::optional<Segment> recall(std::uint64_t entryHash) const;

    // Stores or replaces a segment. A new segment replaces an existing one for
    // the same entry hash only if it is strictly better: further progress for
    // the same frame cost, or the same progress for fewer frames.
    void remember(Segment const& segment);

    // Called when a remembered segment failed on replay. The entry is dropped
    // rather than kept with a penalty, because a segment that does not
    // reproduce is worse than no segment: it wastes an attempt every time.
    void forget(std::uint64_t entryHash, char const* reason);

    std::size_t segmentCount() const { return m_segments.size(); }

    // Total frames covered by remembered segments - the headline "how much of
    // this level do I already know" number.
    Frame knownFrames() const;

    // --- deaths ----------------------------------------------------------

    void recordDeath(Frame frame);
    int deathsAround(Frame frame, Frame radius) const;
    Frame worstDeathFrame() const;

    // --- best macro ------------------------------------------------------

    std::optional<Macro> const& best() const { return m_best; }
    // Keeps whichever macro got further; a certified macro always beats an
    // uncertified one at equal progress.
    void offerBest(Macro const& macro);

    void reset();

private:
    std::string m_levelKey;
    std::string m_levelName;
    std::unordered_map<std::uint64_t, Segment> m_segments;
    std::unordered_map<Frame, int> m_deaths;
    std::optional<Macro> m_best;
};

}  // namespace gdubai
