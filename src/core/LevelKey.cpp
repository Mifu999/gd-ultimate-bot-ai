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

    int const levelId = level->m_levelID.value();
    if (levelId > 0) {
        // Official levels reuse low ids; m_levelType disambiguates them.
        char buffer[48];
        if (level->m_levelType == GJLevelType::Local && levelId < 100) {
            std::snprintf(buffer, sizeof(buffer), "official-%d", levelId);
        } else {
            std::snprintf(buffer, sizeof(buffer), "online-%d", levelId);
        }
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
