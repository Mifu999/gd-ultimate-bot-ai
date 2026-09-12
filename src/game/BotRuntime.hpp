#pragma once

#include "../bot/Arbiter.hpp"
#include "../practice/PracticeDirector.hpp"

#include <memory>
#include <string>

class PlayLayer;

namespace gdubai {

// The single place hooks and UI talk to.
//
// Two run modes share one runtime:
//
//   Practice   the checkpoint-anchored incremental director. Solves the level
//              in place, rolling back checkpoints when it gets stuck, and
//              builds the macro as it goes. This is the mode this project was
//              built for.
//
//   Offline    the classic Pathfinder behaviour: hand the whole level string to
//              the solver on a worker thread and wait for a complete answer.
//              Kept because it is genuinely better on short, fully-modelled
//              levels where the simulator can just solve the thing outright.
class BotRuntime {
public:
    enum class Mode { Practice, Offline };

    static BotRuntime& get();

    bool startPractice(PlayLayer* layer);
    void stop(char const* reason);

    bool running() const { return m_director.active(); }
    Mode mode() const { return m_mode; }

    PracticeDirector& director() { return m_director; }
    PracticeDirector const& director() const { return m_director; }
    Arbiter& arbiter() { return m_arbiter; }

    // Reads the current mod settings into a PracticeConfig.
    PracticeConfig configFromSettings() const;

    // Writes the current macro next to the mod's save dir and returns the path,
    // or an empty path on failure. `format` is "json" or "gdr2".
    std::string exportMacro(std::string const& format);

    std::string const& lastMessage() const { return m_message; }
    void setMessage(std::string message) { m_message = std::move(message); }

private:
    BotRuntime() = default;

    Mode m_mode = Mode::Practice;
    PracticeDirector m_director;
    Arbiter m_arbiter;
    std::string m_message;
};

}  // namespace gdubai
