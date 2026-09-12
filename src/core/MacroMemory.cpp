#include "MacroMemory.hpp"

#include "LevelKey.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace gdubai {

namespace {

constexpr int kFileVersion = 1;

// Deaths are bucketed rather than stored per exact frame. At 240 TPS a level is
// tens of thousands of frames long and two deaths one frame apart mean the same
// thing; bucketing keeps the file small and the histogram readable.
constexpr Frame kDeathBucket = 8;

inline Frame bucketOf(Frame frame) { return frame / kDeathBucket; }

matjson::Value segmentToJson(Segment const& segment) {
    auto object = matjson::Value();
    object["start"] = static_cast<int64_t>(segment.startFrame);
    object["end"] = static_cast<int64_t>(segment.endFrame);
    object["startX"] = segment.startX;
    object["endX"] = segment.endX;
    object["entry"] = std::to_string(segment.entryHash);
    object["exit"] = std::to_string(segment.exitHash);
    object["source"] = std::string(sourceName(segment.source));
    object["attempts"] = static_cast<int64_t>(segment.attempts);
    object["seconds"] = segment.solveSeconds;
    object["certified"] = segment.certified;

    auto flat = matjson::Value::array();
    for (auto const& event : segment.events) {
        flat.push(static_cast<int64_t>(event.frame));
        flat.push(static_cast<int64_t>(event.button));
        flat.push(static_cast<int64_t>(event.player2 ? 1 : 0));
        flat.push(static_cast<int64_t>(event.down ? 1 : 0));
    }
    object["inputs"] = flat;
    return object;
}

Source sourceFromName(std::string const& name) {
    if (name == "Pathfinder") return Source::Pathfinder;
    if (name == "NEAT") return Source::Neat;
    if (name == "Climber") return Source::Climber;
    if (name == "Memory") return Source::Memory;
    if (name == "Human") return Source::Human;
    return Source::Unknown;
}

std::optional<Segment> segmentFromJson(matjson::Value const& object) {
    Segment segment;

    auto readInt = [&](char const* key, auto& target) {
        if (auto value = object[key].asInt(); value.isOk()) {
            target = static_cast<std::decay_t<decltype(target)>>(value.unwrap());
        }
    };
    auto readHash = [&](char const* key, std::uint64_t& target) {
        if (auto value = object[key].asString(); value.isOk()) {
            target = std::strtoull(value.unwrap().c_str(), nullptr, 10);
        }
    };

    readInt("start", segment.startFrame);
    readInt("end", segment.endFrame);
    readInt("attempts", segment.attempts);
    readHash("entry", segment.entryHash);
    readHash("exit", segment.exitHash);

    if (auto value = object["startX"].asDouble(); value.isOk()) {
        segment.startX = static_cast<float>(value.unwrap());
    }
    if (auto value = object["endX"].asDouble(); value.isOk()) {
        segment.endX = static_cast<float>(value.unwrap());
    }
    if (auto value = object["seconds"].asDouble(); value.isOk()) {
        segment.solveSeconds = value.unwrap();
    }
    if (auto value = object["certified"].asBool(); value.isOk()) {
        segment.certified = value.unwrap();
    }
    if (auto value = object["source"].asString(); value.isOk()) {
        segment.source = sourceFromName(value.unwrap());
    }

    auto inputs = object["inputs"];
    if (inputs.isArray()) {
        auto array = inputs.asArray();
        if (!array.isOk()) return std::nullopt;
        auto const& flat = array.unwrap();
        if (flat.size() % 4 != 0) return std::nullopt;
        segment.events.reserve(flat.size() / 4);
        for (std::size_t i = 0; i + 3 < flat.size(); i += 4) {
            auto frame = flat[i].asInt();
            auto button = flat[i + 1].asInt();
            auto player2 = flat[i + 2].asInt();
            auto down = flat[i + 3].asInt();
            if (!frame.isOk() || !button.isOk() || !player2.isOk() || !down.isOk()) {
                return std::nullopt;
            }
            segment.events.push_back(InputEvent{
                static_cast<Frame>(frame.unwrap()),
                static_cast<std::uint8_t>(button.unwrap()),
                player2.unwrap() != 0,
                down.unwrap() != 0,
            });
        }
    }

    if (segment.entryHash == 0 || segment.endFrame <= segment.startFrame) {
        return std::nullopt;
    }
    return segment;
}

}  // namespace

std::filesystem::path MacroMemory::directory() {
    return Mod::get()->getSaveDir() / "memory";
}

std::filesystem::path MacroMemory::fileFor(std::string const& levelKey) {
    return directory() / (sanitiseFileName(levelKey) + ".json");
}

MacroMemory MacroMemory::load(std::string const& levelKey, bool* corrupt) {
    if (corrupt) *corrupt = false;

    MacroMemory memory;
    memory.m_levelKey = levelKey;

    auto const path = fileFor(levelKey);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return memory;

    auto read = utils::file::readJson(path);
    if (!read) {
        log::warn("GDUBAI: memory file {} unreadable: {}", path.string(), read.unwrapErr());
        if (corrupt) *corrupt = true;
        return memory;
    }

    auto const root = read.unwrap();

    if (auto name = root["levelName"].asString(); name.isOk()) {
        memory.m_levelName = name.unwrap();
    }

    if (auto segments = root["segments"]; segments.isArray()) {
        if (auto array = segments.asArray(); array.isOk()) {
            for (auto const& entry : array.unwrap()) {
                if (auto segment = segmentFromJson(entry)) {
                    memory.m_segments[segment->entryHash] = *segment;
                }
            }
        }
    }

    if (auto deaths = root["deaths"]; deaths.isArray()) {
        if (auto array = deaths.asArray(); array.isOk()) {
            auto const& flat = array.unwrap();
            for (std::size_t i = 0; i + 1 < flat.size(); i += 2) {
                auto bucket = flat[i].asInt();
                auto count = flat[i + 1].asInt();
                if (bucket.isOk() && count.isOk()) {
                    memory.m_deaths[static_cast<Frame>(bucket.unwrap())] =
                        static_cast<int>(count.unwrap());
                }
            }
        }
    }

    if (auto best = root["best"]; best.isObject()) {
        if (auto macro = Macro::fromJson(best)) {
            memory.m_best = std::move(macro);
        }
    }

    log::info("GDUBAI: memory for {} loaded - {} segment(s), {} frame(s) known",
              levelKey, memory.m_segments.size(), memory.knownFrames());
    return memory;
}

bool MacroMemory::save() const {
    auto const dir = directory();
    if (auto created = utils::file::createDirectoryAll(dir); !created) {
        log::error("GDUBAI: cannot create {}: {}", dir.string(), created.unwrapErr());
        return false;
    }

    auto root = matjson::Value();
    root["version"] = kFileVersion;
    root["levelKey"] = m_levelKey;
    root["levelName"] = m_levelName;

    auto segments = matjson::Value::array();
    for (auto const& [hash, segment] : m_segments) {
        segments.push(segmentToJson(segment));
    }
    root["segments"] = segments;

    auto deaths = matjson::Value::array();
    for (auto const& [bucket, count] : m_deaths) {
        deaths.push(static_cast<int64_t>(bucket));
        deaths.push(static_cast<int64_t>(count));
    }
    root["deaths"] = deaths;

    if (m_best) root["best"] = m_best->toJson();

    // writeStringSafe writes to a temp file and renames, so a crash mid-write
    // cannot leave a half-written memory that would fail to parse next boot.
    auto written = utils::file::writeStringSafe(fileFor(m_levelKey), root.dump());
    if (!written) {
        log::error("GDUBAI: memory save failed: {}", written.unwrapErr());
        return false;
    }
    return true;
}

std::optional<Segment> MacroMemory::recall(std::uint64_t entryHash) const {
    auto it = m_segments.find(entryHash);
    if (it == m_segments.end()) return std::nullopt;
    return it->second;
}

void MacroMemory::remember(Segment const& segment) {
    if (segment.entryHash == 0 || segment.endFrame <= segment.startFrame) return;

    auto it = m_segments.find(segment.entryHash);
    if (it != m_segments.end()) {
        auto const& existing = it->second;

        // Prefer more ground covered; break ties by fewer frames spent, then by
        // certification. A certified segment is one that survived a full
        // normal-mode replay, which is strictly stronger evidence.
        float const gainNew = segment.endX - segment.startX;
        float const gainOld = existing.endX - existing.startX;

        bool better = gainNew > gainOld + 0.5f;
        if (!better && gainNew > gainOld - 0.5f) {
            if (segment.length() < existing.length()) better = true;
            else if (segment.certified && !existing.certified) better = true;
        }
        if (!better) return;
    }

    m_segments[segment.entryHash] = segment;
}

void MacroMemory::forget(std::uint64_t entryHash, char const* reason) {
    if (m_segments.erase(entryHash)) {
        log::info("GDUBAI: dropped remembered segment {} ({})", entryHash, reason);
    }
}

Frame MacroMemory::knownFrames() const {
    Frame total = 0;
    for (auto const& [hash, segment] : m_segments) total += segment.length();
    return total;
}

void MacroMemory::recordDeath(Frame frame) {
    ++m_deaths[bucketOf(frame)];
}

int MacroMemory::deathsAround(Frame frame, Frame radius) const {
    Frame const centre = bucketOf(frame);
    Frame const span = std::max<Frame>(1, radius / kDeathBucket);
    Frame const low = centre > span ? centre - span : 0;

    int total = 0;
    for (Frame bucket = low; bucket <= centre + span; ++bucket) {
        if (auto it = m_deaths.find(bucket); it != m_deaths.end()) total += it->second;
    }
    return total;
}

Frame MacroMemory::worstDeathFrame() const {
    Frame worstBucket = 0;
    int worstCount = 0;
    for (auto const& [bucket, count] : m_deaths) {
        if (count > worstCount) {
            worstCount = count;
            worstBucket = bucket;
        }
    }
    return worstBucket * kDeathBucket;
}

void MacroMemory::offerBest(Macro const& macro) {
    if (!m_best) {
        m_best = macro;
        return;
    }
    if (macro.reachedPercent > m_best->reachedPercent + 1e-6) {
        m_best = macro;
        return;
    }
    if (std::abs(macro.reachedPercent - m_best->reachedPercent) < 1e-6 &&
        macro.certified && !m_best->certified) {
        m_best = macro;
    }
}

void MacroMemory::reset() {
    m_segments.clear();
    m_deaths.clear();
    m_best.reset();
}

}  // namespace gdubai
