#include "RngScan.hpp"

#include <cstdio>
#include <cstdlib>

namespace gdubai {

namespace {

// Object IDs from the gd-info-explorer object table (FlowVix), GD 2.2:
//   1912  Random Trigger                     [RandomTrigger]
//   2068  Advanced Random Trigger            [AdvancedRandomTrigger]
//   3016  Advanced Follow Trigger            [AdvancedFollowTrigger]
//   3660  Edit Advanced Follow Trigger       [EditAdvancedFollowTrigger]
//
// 3661 (Re-Target Advanced Follow) is deliberately NOT listed: the RNG article
// names only the four above as calling rand(), and we do not want to raise a
// false alarm on a trigger we cannot confirm consumes the seed.
struct KnownTrigger {
    int id;
    char const* name;
};

constexpr KnownTrigger kRngTriggers[] = {
    {1912, "Random"},
    {2068, "AdvRand"},
    {3016, "AdvFollow"},
    {3660, "EditAdvFollow"},
};

// A level string object is `key,value,key,value,...` and key 1 is the object
// ID. Objects are separated by ';'. We only need the leading `1,<id>` of each
// object, which in practice is always first - but we do not rely on that and
// walk the key/value pairs until we find key 1.
int objectIdOf(char const* begin, char const* end) {
    char const* cursor = begin;
    while (cursor < end) {
        // read key
        char const* keyStart = cursor;
        while (cursor < end && *cursor != ',') ++cursor;
        int key = std::atoi(std::string(keyStart, cursor).c_str());
        if (cursor >= end) return 0;
        ++cursor;  // skip comma

        char const* valueStart = cursor;
        while (cursor < end && *cursor != ',') ++cursor;
        if (key == 1) {
            return std::atoi(std::string(valueStart, cursor).c_str());
        }
        if (cursor < end) ++cursor;
    }
    return 0;
}

}  // namespace

RngReport scanForRng(std::string const& levelString) {
    RngReport report;
    int counts[std::size(kRngTriggers)] = {};

    char const* cursor = levelString.data();
    char const* end = cursor + levelString.size();

    // The first ';'-segment is the level header (kA1, kA2, ...), not an object.
    // Skipping it costs nothing and avoids a bogus id from a header key.
    bool first = true;

    while (cursor < end) {
        char const* segmentEnd = cursor;
        while (segmentEnd < end && *segmentEnd != ';') ++segmentEnd;

        if (!first && segmentEnd > cursor) {
            int id = objectIdOf(cursor, segmentEnd);
            for (std::size_t i = 0; i < std::size(kRngTriggers); ++i) {
                if (kRngTriggers[i].id == id) {
                    ++counts[i];
                    break;
                }
            }
        }
        first = false;
        cursor = segmentEnd < end ? segmentEnd + 1 : end;
    }

    for (std::size_t i = 0; i < std::size(kRngTriggers); ++i) {
        if (counts[i] == 0) continue;
        report.found.push_back(
            RngTriggerInfo{kRngTriggers[i].id, kRngTriggers[i].name, counts[i]});
        report.totalCount += counts[i];
    }
    return report;
}

std::string RngReport::summary() const {
    if (totalCount == 0) {
        return "No RNG triggers - this level is deterministic.";
    }

    std::string detail;
    for (std::size_t i = 0; i < found.size(); ++i) {
        if (i) detail += ", ";
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s x%d", found[i].name, found[i].count);
        detail += buffer;
    }

    char head[96];
    std::snprintf(
        head, sizeof(head), "%d RNG trigger%s (", totalCount, totalCount == 1 ? "" : "s");
    return std::string(head) + detail +
           ") - the seed is redrawn every attempt, so a finished macro may not reproduce.";
}

}  // namespace gdubai
