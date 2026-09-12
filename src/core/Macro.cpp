#include "Macro.hpp"

// Needed for GEODE_COMP_GD_VERSION and log::error. The build force-includes a
// PCH that already pulls these in, but relying on that is fragile.
#include <Geode/Geode.hpp>

#include <gdr/gdr.hpp>

#include <algorithm>

namespace gdubai {

namespace {

// GDR replay type for our exports. The name string is what other mods show in
// their macro browser, so it is worth being explicit about the origin.
class GdubaiReplay : public gdr::Replay<GdubaiReplay, gdr::Input<"">> {
public:
    GdubaiReplay() : Replay("GDUltimateBotAI", 1) {}
};

inline bool validButton(int button) {
    return button >= kButtonJump && button <= kButtonRight;
}

}  // namespace

void Macro::append(std::vector<InputEvent> const& incoming) {
    for (auto const& event : incoming) {
        if (!validButton(event.button)) continue;

        auto& state = m_held[event.player2 ? 1 : 0][event.button];
        if (state == event.down) {
            // Already in that state - writing it would be a no-op transition
            // that some replay players count as a click. Drop it.
            continue;
        }
        state = event.down;
        m_events.push_back(event);
    }

    // Cheap guard: appends should already arrive in order, but a solver bug
    // that broke ordering would corrupt every downstream consumer silently.
    if (!std::is_sorted(m_events.begin(), m_events.end(),
                        [](InputEvent const& a, InputEvent const& b) {
                            return a.frame < b.frame;
                        })) {
        std::stable_sort(m_events.begin(), m_events.end(),
                         [](InputEvent const& a, InputEvent const& b) {
                             return a.frame < b.frame;
                         });
        rebuildHeldState();
    }
}

void Macro::truncate(std::size_t count) {
    if (count >= m_events.size()) return;
    m_events.resize(count);
    rebuildHeldState();
}

std::size_t Macro::truncateFromFrame(Frame frame) {
    auto it = std::lower_bound(
        m_events.begin(), m_events.end(), frame,
        [](InputEvent const& event, Frame value) { return event.frame < value; });
    std::size_t removed = static_cast<std::size_t>(std::distance(it, m_events.end()));
    m_events.erase(it, m_events.end());
    if (removed) rebuildHeldState();
    return removed;
}

void Macro::clear() {
    m_events.clear();
    rebuildHeldState();
}

bool Macro::held(int button, bool player2) const {
    if (!validButton(button)) return false;
    return m_held[player2 ? 1 : 0][button];
}

void Macro::rebuildHeldState() {
    for (auto& player : m_held) {
        for (auto& button : player) button = false;
    }
    for (auto const& event : m_events) {
        if (!validButton(event.button)) continue;
        m_held[event.player2 ? 1 : 0][event.button] = event.down;
    }
}

matjson::Value Macro::toJson() const {
    auto root = matjson::Value();
    root["version"] = 1;
    root["levelKey"] = levelKey;
    root["levelName"] = levelName;
    root["tps"] = static_cast<int64_t>(tps);
    root["source"] = std::string(sourceName(dominantSource));
    root["certified"] = certified;
    root["percent"] = reachedPercent;

    // Flat int array rather than an array of objects: a 40k-input macro is
    // ~4x smaller and parses noticeably faster this way.
    auto flat = matjson::Value::array();
    for (auto const& event : m_events) {
        flat.push(static_cast<int64_t>(event.frame));
        flat.push(static_cast<int64_t>(event.button));
        flat.push(static_cast<int64_t>(event.player2 ? 1 : 0));
        flat.push(static_cast<int64_t>(event.down ? 1 : 0));
    }
    root["inputs"] = flat;
    return root;
}

std::optional<Macro> Macro::fromJson(matjson::Value const& value) {
    Macro macro;

    if (auto key = value["levelKey"].asString(); key.isOk()) macro.levelKey = key.unwrap();
    if (auto name = value["levelName"].asString(); name.isOk()) macro.levelName = name.unwrap();
    if (auto tps = value["tps"].asInt(); tps.isOk()) {
        macro.tps = static_cast<std::uint32_t>(tps.unwrap());
    }
    if (auto certified = value["certified"].asBool(); certified.isOk()) {
        macro.certified = certified.unwrap();
    }
    if (auto percent = value["percent"].asDouble(); percent.isOk()) {
        macro.reachedPercent = percent.unwrap();
    }

    auto inputs = value["inputs"];
    if (!inputs.isArray()) return macro;

    auto array = inputs.asArray();
    if (!array.isOk()) return macro;

    auto const& flat = array.unwrap();
    if (flat.size() % 4 != 0) {
        // Truncated or foreign file - refuse rather than importing a macro that
        // is silently offset by one field.
        return std::nullopt;
    }

    std::vector<InputEvent> parsed;
    parsed.reserve(flat.size() / 4);
    for (std::size_t i = 0; i + 3 < flat.size(); i += 4) {
        auto frame = flat[i].asInt();
        auto button = flat[i + 1].asInt();
        auto player2 = flat[i + 2].asInt();
        auto down = flat[i + 3].asInt();
        if (!frame.isOk() || !button.isOk() || !player2.isOk() || !down.isOk()) {
            return std::nullopt;
        }
        parsed.push_back(InputEvent{
            static_cast<Frame>(frame.unwrap()),
            static_cast<std::uint8_t>(button.unwrap()),
            player2.unwrap() != 0,
            down.unwrap() != 0,
        });
    }

    macro.append(parsed);
    return macro;
}

std::vector<std::uint8_t> Macro::toGdr2() const {
    // Field names per the GDR2 spec (GDReplayFormat@gdr2, readme.md):
    //   Replay { author, description, duration, gameVersion, framerate, seed,
    //            coins, ldm, platformer, botInfo, levelInfo, inputs, deaths }
    //   Level  { uint32_t id; std::string name; }
    // Note it is `levelInfo`, not `level`.
    GdubaiReplay replay;
    replay.author = "GDUltimateBotAI";
    replay.description = certified ? "Certified in normal mode."
                                   : "Built in practice mode; not validated.";
    replay.framerate = static_cast<double>(tps);
    replay.gameVersion = GEODE_COMP_GD_VERSION;
    replay.seed = seed;
    replay.levelInfo.id = levelId;
    replay.levelInfo.name = levelName;
    replay.duration = tps ? static_cast<float>(lastFrame()) / static_cast<float>(tps) : 0.f;

    for (auto const& event : m_events) {
        replay.inputs.push_back(
            gdr::Input(event.frame, static_cast<int>(event.button), event.player2, event.down));
    }

    // No synthetic leading input. The Pathfinder fork inserts a dummy release at
    // frame 1, but the GDR2 spec only asks that the first frame be 0, and a
    // phantom release can itself confuse a replayer.

    // exportData() takes no arguments and returns Result<std::vector<uint8_t>>.
    // The overload that takes a path returns Result<> and writes the file.
    auto exported = replay.exportData();
    if (!exported) {
        geode::log::error("GDUBAI: gdr export failed: {}", exported.unwrapErr());
        return {};
    }
    return exported.unwrap();
}

}  // namespace gdubai
