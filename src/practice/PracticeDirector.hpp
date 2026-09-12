#pragma once

#include "../core/Macro.hpp"
#include "../core/MacroMemory.hpp"
#include "../core/RngScan.hpp"
#include "../core/Types.hpp"
#include "CheckpointLedger.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

namespace gdubai {

class Arbiter;

// Tuning knobs, all surfaced as mod settings. Defaults are deliberately
// conservative: the director should look boring and correct before it looks
// fast.
struct PracticeConfig {
    // How many frames must survive past an anchor before we trust the stretch
    // and plant the next checkpoint. Too short and checkpoints pile up (each
    // one is a full state snapshot - they are not free); too long and a single
    // hard moment forces re-solving a lot of already-good ground.
    Frame commitStride = 120;      // 0.5 s at 240 TPS

    // How far ahead a solver is asked to plan from an anchor.
    Frame planHorizon = 480;       // 2 s
    Frame planHorizonMax = 2880;   // 12 s, reached through rollback escalation

    // Attempts spent on one anchor before dropping a checkpoint and rewinding.
    std::uint32_t retryBudget = 24;

    // Wall-clock ceiling for one anchor, so a pathological spot cannot hang the
    // session forever.
    double anchorSeconds = 25.0;

    // Only plant a checkpoint on a frame where every button is released.
    // Strongly recommended: it sidesteps the PlayerCheckpoint held-button gap
    // entirely. When false the director re-applies the held state manually
    // after each respawn, which works but has more moving parts.
    bool checkpointOnReleaseOnly = true;

    // Maximum number of live checkpoints. GD itself has no hard cap, but each
    // CheckpointObject stores a full GJGameState plus saved object state, so a
    // dense level with thousands of them will hurt. Oldest are dropped first.
    std::size_t maxCheckpoints = 400;

    // Replay the finished macro from frame 0 in NORMAL mode before declaring it
    // good. See ARCHITECTURE.md - practice restore is close to exact but not
    // provably exact, so this pass is what turns "it worked in practice" into
    // "this macro actually plays the level".
    bool validateInNormalMode = true;

    // Position/velocity error (in units) above which gd-sim is considered to
    // have diverged from the real game at a commit point.
    double simDivergenceTolerance = 1.5;
};

class PracticeDirector {
public:
    enum class Phase {
        Idle,
        Preparing,
        Planning,
        Running,
        RollingBack,
        Validating,
        Finished,
        Failed,
    };

    PracticeDirector();
    ~PracticeDirector();

    // --- lifecycle -------------------------------------------------------

    // Takes over the given PlayLayer: forces practice mode on, clears existing
    // checkpoints, loads memory, scans for RNG triggers. Returns false and sets
    // lastError() if the level cannot be driven (no level string, etc).
    bool begin(PlayLayer* layer, PracticeConfig config, Arbiter* arbiter);

    // Restores practice mode and checkpoints to whatever the player had before
    // begin(). Safe to call from any phase.
    void stop(char const* reason);

    bool active() const { return m_phase != Phase::Idle; }
    Phase phase() const { return m_phase; }
    std::string const& lastError() const { return m_lastError; }

    // --- per-frame driving ----------------------------------------------

    // Called from GJBaseGameLayer::processCommands, before the game steps.
    // Emits the planned inputs for the current frame through handleButton.
    void onProcessCommands(PlayLayer* layer);

    // Called from PlayLayer::postUpdate, after the game has stepped. This is
    // where commit / rollback decisions are taken, because the player state is
    // settled by then.
    void onPostUpdate(PlayLayer* layer);

    // Called from PlayLayer::destroyPlayer when player 1 actually dies.
    // Returns true if the director handled the death and the caller should
    // suppress the vanilla death sequence.
    bool onPlayerDeath(PlayLayer* layer, PlayerObject* player);

    // Called from PlayLayer::levelComplete.
    void onLevelComplete(PlayLayer* layer);

    // Called from PlayLayer::resetLevel AFTER the original ran, so the game has
    // already respawned us at the last checkpoint.
    void onLevelReset(PlayLayer* layer);

    void onQuit(PlayLayer* layer);

    // --- results ---------------------------------------------------------

    Macro const& macro() const { return m_macro; }
    MacroMemory& memory() { return m_memory; }
    RngReport const& rngReport() const { return m_rng; }

    // One or two lines for the HUD.
    std::string statusLine() const;
    std::string detailLine() const;

    double progressPercent() const { return m_bestPercent; }
    std::uint32_t rollbackCount() const { return m_rollbacks; }
    std::uint64_t attemptCount() const { return m_attempts; }

private:
    // --- state machine steps --------------------------------------------

    void enterPlanning(PlayLayer* layer);
    void commitStretch(PlayLayer* layer);
    void failAttempt(PlayLayer* layer, Frame deathFrame);
    void rollback(PlayLayer* layer, char const* reason);
    void beginValidation(PlayLayer* layer);
    void finish(PlayLayer* layer, bool solved, char const* reason);

    // --- helpers ---------------------------------------------------------

    AnchorState sampleState(PlayLayer* layer) const;
    Frame currentFrame(PlayLayer* layer) const;
    bool plantCheckpoint(PlayLayer* layer);
    void dropCheckpoint(PlayLayer* layer);
    void reapplyHeldButtons(PlayLayer* layer, Anchor const& anchor);
    void releaseAllButtons(PlayLayer* layer);
    bool allButtonsReleased() const;
    void flushMemory(bool force);

    Phase m_phase = Phase::Idle;
    PracticeConfig m_config;
    Arbiter* m_arbiter = nullptr;

    CheckpointLedger m_ledger;
    Macro m_macro;
    MacroMemory m_memory;
    RngReport m_rng;

    // The plan currently being executed, in absolute frames.
    std::vector<InputEvent> m_plan;
    std::size_t m_planCursor = 0;
    Source m_planSource = Source::Unknown;
    Frame m_planStart = 0;
    Frame m_planEnd = 0;
    bool m_planFromMemory = false;

    // Live button state we are driving, so we never emit a redundant event.
    bool m_driven[2][4] = {};

    // Session bookkeeping.
    std::uint64_t m_attempts = 0;
    std::uint32_t m_rollbacks = 0;
    double m_bestPercent = 0.0;
    Frame m_furthestFrame = 0;
    std::chrono::steady_clock::time_point m_anchorStart;
    std::chrono::steady_clock::time_point m_sessionStart;
    std::string m_lastError;
    std::string m_note;

    // Saved player preferences, restored by stop().
    bool m_hadPracticeMode = false;
    bool m_restorePracticeMode = false;

    // Validation pass state.
    bool m_validating = false;
    std::size_t m_validateCursor = 0;
    Frame m_validateDivergedAt = 0;

    std::size_t m_dirtyWrites = 0;
    std::chrono::steady_clock::time_point m_lastFlush;
};

}  // namespace gdubai
