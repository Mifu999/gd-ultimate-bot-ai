#include "LevelKey.hpp"

#include <Geode/Geode.hpp>

#include <cstdio>

using namespace geode::prelude;

namespace gdubai {

std::string decompressedLevelString(GJGameLevel* level) {
    if (!level) return {};

    std::string encoded = level->m_levelString.c_str();
    if (encoded.empty()) return {};

    // ZipUtils::decompressString returns an empty string when the input was not
    // actually gzip+base64 - which is the case for a level being edited live.
    auto decoded = ZipUtils::decompressString(encoded, true, 0);
    if (!decoded.empty()) return decoded;
    return encoded;
}

std::string sanitiseFileName(std::string const& input) {
    std::string output;
    output.reserve(input.size());
    for (char c : input) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        output.push_back(ok ? c : '_');
    }
    if (output.empty()) output = "unnamed";
    if (output.size() > 96) output.resize(96);
    return output;
}

std::string levelKeyFor(GJGameLevel* level) {
    if (!level) return "none";

    // GJLevelType is { Default=0, Main=1, Editor=2, Saved=3, SearchResult=4 }.
    // Main is the 22 built-in levels; there is no "Local" member.
    //
    // Editor levels are deliberately keyed by content hash rather than by id,
    // even when they have one: the local copy can be edited after upload, and a
    // macro for the old layout must not be recalled for the new one.
    int const levelId = level->m_levelID.value();
    if (levelId > 0 && level->m_levelType != GJLevelType::Editor) {
        char buffer[48];
        std::snprintf(
            buffer, sizeof(buffer),
            level->m_levelType == GJLevelType::Main ? "official-%d" : "online-%d",
            levelId);
        return buffer;
    }

    auto const data = decompressedLevelString(level);
    if (!data.empty()) {
        // FNV-1a over the content. Collisions are irrelevant here: the worst
        // case is two unrelated local levels sharing a memory file, and the
        // per-anchor state hashes inside it would simply never match.
        std::uint64_t h = 1469598103934665603ull;
        for (unsigned char c : data) {
            h ^= c;
            h *= 1099511628211ull;
        }
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "local-%016llx",
                      static_cast<unsigned long long>(h));
        return buffer;
    }

    return "local-" + sanitiseFileName(level->m_levelName.c_str());
}

}  // namespace gdubai
