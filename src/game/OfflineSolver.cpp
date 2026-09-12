#include "OfflineSolver.hpp"

#include "../core/LevelKey.hpp"
#include "../core/RngScan.hpp"
#include "../solver/pathfinder.hpp"
#include "BotRuntime.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace gdubai {

OfflineSolver& OfflineSolver::get() {
    static OfflineSolver instance;
    return instance;
}

bool OfflineSolver::start(PlayLayer* layer) {
    if (m_busy.load()) {
        BotRuntime::get().setMessage("The offline solver is already running.");
        return false;
    }
    if (BotRuntime::get().running()) {
        BotRuntime::get().setMessage(
            "Stop the practice run first - both modes drive the same player.");
        return false;
    }
    if (!layer || !layer->m_level) {
        BotRuntime::get().setMessage("Open a level first.");
        return false;
    }

    auto const levelString = decompressedLevelString(layer->m_level);
    if (levelString.empty()) {
        BotRuntime::get().setMessage("This level has no readable level string.");
        return false;
    }

    auto const rng = scanForRng(levelString);
    if (!rng.deterministic()) {
        log::warn("GDUBAI (offline): {}", rng.summary());
    }

    m_result = Macro{};
    m_result.levelKey = levelKeyFor(layer->m_level);
    m_result.levelName = layer->m_level->m_levelName.c_str();
    m_result.dominantSource = Source::Pathfinder;

    // getEndPosition() is `win inline` - callable via the codegen's
    // reconstructed body, just not hookable. It is the authoritative playable
    // endpoint, which the solver needs so it does not chase decorations.
    float const endX = layer->getEndPosition().x;

    m_stop = false;
    m_busy = true;
    m_progress = 0.0;
    m_trials = 0;
    m_note.clear();

    m_worker = std::async(std::launch::async, [this, levelString, endX] {
        try {
            auto result = pathfind(
                levelString, m_stop,
                [this](PathfinderTelemetry const& telemetry) {
                    if (m_progress.load() < telemetry.progress) {
                        m_progress = telemetry.progress;
                    }
                    m_vehicle = telemetry.vehicleType;
                    m_trials = telemetry.totalTrials;
                },
                endX);

            std::vector<InputEvent> events;
            events.reserve(result.inputs.size());
            for (auto const& input : result.inputs) {
                events.push_back(InputEvent{
                    static_cast<Frame>(input.frame),
                    static_cast<std::uint8_t>(input.button),
                    input.player2,
                    input.down,
                });
            }

            // The macro is only touched from the worker before m_busy drops, and
            // read from the main thread after - poll() is the handoff point.
            m_result.append(events);
            m_result.reachedPercent = result.progress;
            m_note = result.diagnostics;
        } catch (std::exception const& e) {
            m_note = std::string("solver threw: ") + e.what();
            log::error("GDUBAI (offline): {}", m_note);
        } catch (...) {
            m_note = "solver threw an unknown exception";
        }
        m_busy = false;
    });

    BotRuntime::get().setMessage("Offline solve started.");
    return true;
}

void OfflineSolver::stop() {
    if (!m_busy.load() && !m_worker.valid()) return;
    m_stop = true;
    if (m_worker.valid()) m_worker.wait();
    m_busy = false;
}

bool OfflineSolver::poll() {
    if (!m_worker.valid()) return false;
    if (m_worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    m_worker.get();

    BotRuntime::get().setMessage(fmt::format(
        "Offline solve finished: {} input(s), {:.2f}%{}", m_result.size(),
        m_result.reachedPercent, m_note.empty() ? "" : (" - " + m_note)));
    return true;
}

std::string OfflineSolver::statusLine() const {
    if (!m_busy.load()) return m_note.empty() ? "Offline solver idle." : m_note;
    return fmt::format("Offline solving... {:.2f}% | {} trial(s)", m_progress.load(),
                       m_trials.load());
}

}  // namespace gdubai
