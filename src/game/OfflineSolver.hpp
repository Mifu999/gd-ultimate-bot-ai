#pragma once

#include "../core/Macro.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <string>

class PlayLayer;

namespace gdubai {

// The classic Pathfinder path, kept alongside the practice director.
//
// It hands the whole decompressed level string to the vendored solver on a
// worker thread and waits for a complete answer. On a short, fully-modelled
// level this simply beats the incremental approach: there is no reason to
// checkpoint your way through something the simulator can solve outright.
//
// The two modes are mutually exclusive - the runtime refuses to start one while
// the other is busy - because both drive the same player.
class OfflineSolver {
public:
    static OfflineSolver& get();

    bool start(PlayLayer* layer);
    void stop();

    bool busy() const { return m_busy.load(); }

    // Polls the worker; call from the main thread. Returns true once, on the
    // frame the result becomes available.
    bool poll();

    Macro const& result() const { return m_result; }
    std::string statusLine() const;

private:
    OfflineSolver() = default;

    std::atomic_bool m_stop{false};
    std::atomic_bool m_busy{false};
    std::atomic<double> m_progress{0.0};
    std::atomic<int> m_vehicle{0};
    std::atomic<unsigned long long> m_trials{0};

    std::future<void> m_worker;
    Macro m_result;
    std::string m_note;
};

}  // namespace gdubai
