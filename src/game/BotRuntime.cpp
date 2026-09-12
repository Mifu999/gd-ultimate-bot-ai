#include "BotRuntime.hpp"

#include "../core/LevelKey.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace gdubai {

BotRuntime& BotRuntime::get() {
    static BotRuntime instance;
    return instance;
}

PracticeConfig BotRuntime::configFromSettings() const {
    auto* mod = Mod::get();
    PracticeConfig config;

    auto readFrame = [&](char const* key, Frame fallback) -> Frame {
        auto value = mod->getSettingValue<int64_t>(key);
        return value > 0 ? static_cast<Frame>(value) : fallback;
    };

    config.commitStride = readFrame("commit-stride", config.commitStride);
    config.planHorizon = readFrame("plan-horizon", config.planHorizon);
    config.planHorizonMax = readFrame("plan-horizon-max", config.planHorizonMax);
    config.retryBudget =
        static_cast<std::uint32_t>(mod->getSettingValue<int64_t>("retry-budget"));
    config.anchorSeconds = mod->getSettingValue<double>("anchor-seconds");
    config.maxCheckpoints =
        static_cast<std::size_t>(mod->getSettingValue<int64_t>("max-checkpoints"));
    config.checkpointOnReleaseOnly =
        mod->getSettingValue<bool>("checkpoint-on-release-only");
    config.validateInNormalMode = mod->getSettingValue<bool>("validate-in-normal-mode");

    // Guard rails: a zero or inverted value here would spin the director
    // forever, and settings files can be hand-edited.
    if (config.retryBudget == 0) config.retryBudget = 1;
    if (config.anchorSeconds < 1.0) config.anchorSeconds = 1.0;
    if (config.maxCheckpoints < 4) config.maxCheckpoints = 4;
    if (config.planHorizonMax < config.planHorizon) {
        config.planHorizonMax = config.planHorizon;
    }
    if (config.commitStride > config.planHorizon) {
        config.commitStride = config.planHorizon;
    }
    return config;
}

bool BotRuntime::startPractice(PlayLayer* layer) {
    if (m_director.active()) {
        m_message = "A run is already in progress.";
        return false;
    }
    if (!layer) {
        m_message = "Open a level first.";
        return false;
    }

    m_mode = Mode::Practice;
    m_arbiter.reset();

    if (!m_director.begin(layer, configFromSettings(), &m_arbiter)) {
        m_message = m_director.lastError();
        return false;
    }

    auto const& rng = m_director.rngReport();
    m_message = rng.deterministic()
                    ? "Practice run started."
                    : std::string("Practice run started. ") + rng.summary();
    return true;
}

void BotRuntime::stop(char const* reason) {
    m_director.stop(reason);
}

std::string BotRuntime::exportMacro(std::string const& format) {
    auto const& macro = m_director.macro();
    if (macro.empty()) {
        m_message = "Nothing to export - the macro is empty.";
        return {};
    }

    auto const dir = Mod::get()->getSaveDir() / "macros";
    if (auto created = utils::file::createDirectoryAll(dir); !created) {
        m_message = "Could not create the macros folder.";
        log::error("GDUBAI: {}: {}", m_message, created.unwrapErr());
        return {};
    }

    auto const stem = sanitiseFileName(
        macro.levelName.empty() ? macro.levelKey : macro.levelName);

    if (format == "gdr2") {
        auto const path = dir / (stem + ".gdr2");
        auto const bytes = macro.toGdr2();
        auto written = utils::file::writeBinarySafe(path, bytes);
        if (!written) {
            m_message = "Failed to write the .gdr2 file.";
            log::error("GDUBAI: {}: {}", m_message, written.unwrapErr());
            return {};
        }
        m_message = fmt::format("Exported {} input(s) to {}", macro.size(), path.string());
        return path.string();
    }

    auto const path = dir / (stem + ".gdubai.json");
    auto written = utils::file::writeStringSafe(path, macro.toJson().dump());
    if (!written) {
        m_message = "Failed to write the macro file.";
        log::error("GDUBAI: {}: {}", m_message, written.unwrapErr());
        return {};
    }
    m_message = fmt::format("Exported {} input(s) to {}", macro.size(), path.string());
    return path.string();
}

}  // namespace gdubai
