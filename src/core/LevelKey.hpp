#pragma once

#include <string>

class GJGameLevel;

namespace gdubai {

// A stable per-level identifier used to name memory and macro files.
//
// Priority order, and why:
//   1. Online level id  -> "online-1234567". Unique and permanent.
//   2. Official level id -> "official-12". The 22 main levels have no online id.
//   3. Local/unlisted    -> "local-<hash of the level string>". An unsaved or
//      unuploaded level has no meaningful id, and its name is not unique, so we
//      fingerprint the content. Editing the level produces a new key, which is
//      correct: the old macro no longer applies.
std::string levelKeyFor(GJGameLevel* level);

// Decompressed `m_levelString`, or the raw string if it was not compressed.
// Returns empty when the level has no data loaded yet.
std::string decompressedLevelString(GJGameLevel* level);

// Filesystem-safe version of an arbitrary string.
std::string sanitiseFileName(std::string const& input);

}  // namespace gdubai
