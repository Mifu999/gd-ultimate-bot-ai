#include "PracticeDirector.hpp"

#include "../bot/Arbiter.hpp"
#include "../core/LevelKey.hpp"
#include "../game/FrameClock.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace gdubai {

namespace {

// GJBaseGameLayer::handleButton's third parameter is isPlayer1, NOT player2.
//
// The bindings name it `isPlayer1` and xdBot - the reference macro bot on
// 2.2081 - calls it as `handleButton(hold, button, !player2)`. Note that the
// Pathfinder fork this project borrows its solver from passes `player2`
// directly, which inverts the two players; that bug is invisible on single
// player levels and breaks every dual level. We follow xdBot.
inline void pressButton(PlayLayer* layer, bool down, int button, bool player2) {
    if (!layer) return;
    layer->GJBaseGameLayer::handleButton(down, button, !player2);
}

constexpr Frame kMinHorizon = 60;

}  // namespace

PracticeDirector::PracticeDirector() = default;
PracticeDirector::~PracticeDirector() = default;

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

bool PracticeDirector::begin(PlayLayer* layer, PracticeConfig config, Arbiter* arbiter) {
    m_lastError.clear();

    if (!layer || !layer->m_level) {
        m_lastError = "No level is loaded.";
        return false;
    }
    if (!layer->m_player1) {
        m_lastError = "PlayLayer has no player yet - start the level first.";
        return false;
    }

    m_config = config;
    m_arbiter = arbiter;
    m_phase = Phase::Preparing;

    auto const levelString = decompressedLevelString(layer->m_level);
    if (levelString.empty()) {
        m_lastError = "This level has no readable level string.";
        m_phase = Phase::Idle;
        return false;
    }

    // Randomness check. This does not stop anything - it decides what we are
    // allowed to promise about the resulting macro. See RngScan.hpp.
    m_rng = scanForRng(levelString);
    if (!m_rng.deterministic()) {
        log::warn("GDUBAI: {}", m_rng.summary());
    }

    auto const key = levelKeyFor(layer->m_level);
    bool corrupt = false;
    m_memory = MacroMemory::load(key, &corrupt);
    m_memory.setLevelKey(key);
    m_memory.setLevelName(layer->m_level->m_levelName.c_str());
    if (corrupt) {
        m_note = "Previous memory file was unreadable and was ignored.";
    }

    m_macro = Macro{};
    m_macro.levelKey = key;
    m_macro.levelName = layer->m_level->m_levelName.c_str();

    // Remember what the player had so stop() can put it back.
    m_hadPracticeMode = layer->m_isPracticeMode;
    m_restorePracticeMode = true;

    // Take the level into practice mode and start from a clean slate. Both of
    // these are addressed functions on every platform, so this is a plain call
    // rather than anything clever.
    if (!layer->m_isPracticeMode) {
        layer->togglePracticeMode(true);
    }
    layer->removeAllCheckpoints();

    FrameClock::get().reset();
    layer->resetLevelFromStart();

    m_ledger.reset(sampleState(layer));
    m_macro.clear();
    m_plan.clear();
    m_planCursor = 0;
    m_attempts = 0;
    m_rollbacks = 0;
    m_bestPercent = 0.0;
    m_furthestFrame = 0;
    m_validating = false;
    m_validateCursor = 0;
    m_validateDivergedAt = 0;
    m_dirtyWrites = 0;
    m_sessionStart = std::chrono::steady_clock::now();
    m_lastFlush = m_sessionStart;

    for (auto& player : m_driven) {
        for (auto& button : player) button = false;
    }

    if (m_arbiter) {
        // getEndPosition() is `win inline` in the bindings, meaning it cannot be
        // HOOKED on Windows - but the codegen emits a reconstructed body, so
        // calling it is fine. It is the only reliable playable endpoint.
        float const endX = layer->getEndPosition().x;
        if (!m_arbiter->prepare(levelString, endX)) {
            log::warn(
                "GDUBAI: gd-sim could not model this level ({}). Falling back to "
                "learner-only mode.",
                m_arbiter->lastNote());
        }
    }

    m_phase = Phase::Planning;
    log::info("GDUBAI: practice director started on {} ({} known frame(s) in memory)",
              key, m_memory.knownFrames());
    return true;
}

void PracticeDirector::stop(char const* reason) {
    if (m_phase == Phase::Idle) return;

    if (auto* layer = PlayLayer::get()) {
        releaseAllButtons(layer);
        if (m_restorePracticeMode && layer->m_isPracticeMode != m_hadPracticeMode) {
            layer->removeAllCheckpoints();
            layer->togglePracticeMode(m_hadPracticeMode);
        }
    }

    flushMemory(true);
    m_phase = Phase::Idle;
    m_plan.clear();
    m_planCursor = 0;
    log::info("GDUBAI: practice director stopped ({})", reason ? reason : "no reason");
}

// ---------------------------------------------------------------------------
// per-frame
// ---------------------------------------------------------------------------

Frame PracticeDirector::currentFrame(PlayLayer* layer) const {
    (void)layer;
    return FrameClock::get().frame();
}

AnchorState PracticeDirector::sampleState(PlayLayer* layer) const {
    AnchorState state;
    if (!layer || !layer->m_player1) return state;

    auto* player = layer->m_player1;
    state.x = player->getPositionX();
    state.y = player->getPositionY();
    state.yVelocity = player->m_yVelocity;
    state.gravityMod = player->m_gravityMod;
    state.frame = FrameClock::get().frame();
    state.mini = player->m_isMini;
    state.upsideDown = player->m_isUpsideDown;
    state.sideways = player->m_isSideways;
    state.onGround = player->m_isOnGround;
    state.dual = layer->m_gameState.m_isDualMode;

    // GD stores the gamemode as a set of booleans rather than an enum; cube is
    // "none of the above".
    if (player->m_isShip)        state.gamemode = 1;
    else if (player->m_isBall)   state.gamemode = 2;
    else if (player->m_isBird)   state.gamemode = 3;
    else if (player->m_isDart)   state.gamemode = 4;
    else if (player->m_isRobot)  state.gamemode = 5;
    else if (player->m_isSpider) state.gamemode = 6;
    else if (player->m_isSwing)  state.gamemode = 7;
    else                         state.gamemode = 0;

    float const speed = player->m_playerSpeed;
    if (speed < 0.8f)       state.speed = 0;
    else if (speed < 1.0f)  state.speed = 1;
    else if (speed < 1.2f)  state.speed = 2;
    else if (speed < 1.45f) state.speed = 3;
    else                    state.speed = 4;

    return state;
}

void PracticeDirector::onProcessCommands(PlayLayer* layer) {
    if (m_phase == Phase::Idle || !layer) return;

    Frame const frame = currentFrame(layer);

    if (m_phase == Phase::Validating) {
        // Straight playback of the finished macro from frame 0 in normal mode.
        auto const& events = m_macro.events();
        while (m_validateCursor < events.size() && events[m_validateCursor].frame <= frame) {
            auto const& event = events[m_validateCursor++];
            pressButton(layer, event.down, event.button, event.player2);
        }
        return;
    }

    if (m_phase != Phase::Running) return;

    while (m_planCursor < m_plan.size() && m_plan[m_planCursor].frame <= frame) {
        auto const& event = m_plan[m_planCursor++];
        auto& state = m_driven[event.player2 ? 1 : 0][event.button];
        if (state == event.down) continue;
        state = event.down;
        pressButton(layer, event.down, event.button, event.player2);
    }
}

void PracticeDirector::onPostUpdate(PlayLayer* layer) {
    if (m_phase == Phase::Idle || !layer || !layer->m_player1) return;

    Frame const frame = currentFrame(layer);
    double const percent = layer->getCurrentPercent();
    if (percent > m_bestPercent) m_bestPercent = percent;
    if (frame > m_furthestFrame) m_furthestFrame = frame;

    switch (m_phase) {
        case Phase::Planning:
            enterPlanning(layer);
            break;

        case Phase::Running: {
            auto const& anchor = m_ledger.top();

            // Survived far enough past the anchor -> lock this stretch in.
            if (frame >= anchor.frame + m_config.commitStride) {
                commitStretch(layer);
                break;
            }

            // The plan ran out without dying and without reaching the stride.
            // That means the solver committed less than commitStride; ask for a
            // continuation rather than treating it as a failure.
            if (m_planCursor >= m_plan.size() && frame >= m_planEnd) {
                m_phase = Phase::Planning;
            }

            // Wall-clock guard so one impossible anchor cannot hang the session.
            auto const elapsed = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - m_anchorStart)
                                     .count();
            if (elapsed > m_config.anchorSeconds) {
                rollback(layer, "anchor time budget exhausted");
            }
            break;
        }

        default:
            break;
    }

    flushMemory(false);
}

bool PracticeDirector::onPlayerDeath(PlayLayer* layer, PlayerObject* player) {
    if (m_phase == Phase::Idle || !layer) return false;
    if (player != layer->m_player1) return false;

    Frame const frame = currentFrame(layer);

    if (m_phase == Phase::Validating) {
        // The macro worked in practice but not from a cold start. That is
        // exactly the divergence this pass exists to catch.
        m_validateDivergedAt = frame;
        log::warn(
            "GDUBAI: validation FAILED at frame {} ({:.2f}%). The macro is valid "
            "under practice-mode restore but not from a cold run.",
            frame, layer->getCurrentPercent());
        m_macro.certified = false;
        finish(layer, false, "validation diverged");
        return false;
    }

    failAttempt(layer, frame);
    return false;
}

void PracticeDirector::onLevelComplete(PlayLayer* layer) {
    if (m_phase == Phase::Idle || !layer) return;

    if (m_phase == Phase::Validating) {
        m_macro.certified = true;
        m_macro.reachedPercent = 100.0;
        m_memory.offerBest(m_macro);
        finish(layer, true, "validated in normal mode");
        return;
    }

    // Commit whatever is still in flight, then move to validation.
    commitStretch(layer);
    m_macro.reachedPercent = 100.0;
    m_memory.offerBest(m_macro);
    flushMemory(true);

    if (m_config.validateInNormalMode) {
        beginValidation(layer);
    } else {
        finish(layer, true, "solved in practice (validation disabled)");
    }
}

void PracticeDirector::onLevelReset(PlayLayer* layer) {
    if (m_phase == Phase::Idle || !layer) return;

    // The game has already put us back wherever it was going to. Resynchronise
    // our frame counter with the anchor we believe we are standing on, because
    // GD's own attempt timer is restored from the checkpoint's GJGameState and
    // our integer counter has to agree with it.
    auto const& anchor = m_ledger.top();
    FrameClock::get().setFrame(anchor.frame);

    for (auto& player : m_driven) {
        for (auto& button : player) button = false;
    }

    if (m_phase == Phase::Validating) {
        m_validateCursor = 0;
        FrameClock::get().setFrame(0);
        return;
    }

    // PlayerCheckpoint does not restore held buttons, so put them back by hand
    // unless we are only ever anchoring on released frames.
    if (!m_config.checkpointOnReleaseOnly) {
        reapplyHeldButtons(layer, anchor);
    }

    m_phase = Phase::Planning;
}

void PracticeDirector::onQuit(PlayLayer* layer) {
    (void)layer;
    if (m_phase == Phase::Idle) return;
    m_macro.reachedPercent = m_bestPercent;
    m_memory.offerBest(m_macro);
    stop("level quit");
}

// ---------------------------------------------------------------------------
// state machine steps
// ---------------------------------------------------------------------------

void PracticeDirector::enterPlanning(PlayLayer* layer) {
    if (!m_ledger.verifyAgainst(layer)) {
        m_lastError = "Checkpoint ledger lost sync with the game.";
        finish(layer, false, "ledger desync");
        return;
    }

    auto& anchor = m_ledger.top();
    anchor.state = anchor.checkpoint ? anchor.state : sampleState(layer);

    Frame horizon = std::min(
        m_config.planHorizonMax,
        m_config.planHorizon + anchor.horizonBoost * m_config.planHorizon);
    horizon = std::max(kMinHorizon, horizon);

    PlanRequest request;
    request.anchor = anchor.state;
    request.anchor.frame = anchor.frame;
    request.horizon = horizon;
    request.attemptIndex = anchor.failures;
    request.deathsNearby = m_memory.deathsAround(anchor.frame, m_config.commitStride * 2);
    request.simDistrusted = anchor.simDistrusted;
    request.budgetSeconds = std::max(0.5, m_config.anchorSeconds / 6.0);

    Plan plan = m_arbiter ? m_arbiter->plan(request, m_memory) : Plan{};

    if (!plan.usable()) {
        // Nobody had anything to offer. Widen and try again; if we are already
        // at the ceiling, rewind.
        if (horizon >= m_config.planHorizonMax) {
            rollback(layer, "no solver produced a usable plan");
        } else {
            ++anchor.horizonBoost;
        }
        return;
    }

    m_plan = std::move(plan.events);
    m_planCursor = 0;
    m_planSource = plan.source;
    m_planStart = plan.start;
    m_planEnd = plan.end;
    m_planFromMemory = plan.source == Source::Memory;
    anchor.fromMemory = m_planFromMemory;

    ++m_attempts;
    m_anchorStart = std::chrono::steady_clock::now();
    m_phase = Phase::Running;
}

void PracticeDirector::commitStretch(PlayLayer* layer) {
    if (!layer || !layer->m_player1) return;

    auto& anchor = m_ledger.top();
    Frame const frame = currentFrame(layer);
    if (frame <= anchor.frame) return;

    // Slice out the part of the plan that actually got played.
    std::vector<InputEvent> played;
    played.reserve(m_planCursor);
    for (std::size_t i = 0; i < m_planCursor && i < m_plan.size(); ++i) {
        if (m_plan[i].frame >= anchor.frame && m_plan[i].frame < frame) {
            played.push_back(m_plan[i]);
        }
    }

    std::size_t const macroBefore = m_macro.size();
    m_macro.append(played);

    auto const exitState = sampleState(layer);

    Segment segment;
    segment.startFrame = anchor.frame;
    segment.endFrame = frame;
    segment.startX = static_cast<float>(anchor.state.x);
    segment.endX = static_cast<float>(exitState.x);
    segment.entryHash = anchor.state.hash();
    segment.exitHash = exitState.hash();
    segment.source = m_planSource;
    segment.attempts = anchor.failures + 1;
    segment.events = played;
    segment.solveSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - m_anchorStart)
            .count();

    // Only teach the memory things the solvers found. Replaying a remembered
    // segment and storing it again would just rewrite the same entry.
    if (!m_planFromMemory) {
        m_memory.remember(segment);
        ++m_dirtyWrites;
    }
    if (m_arbiter) m_arbiter->reportSuccess(segment);

    // --- sim / reality divergence check ---------------------------------
    //
    // If the plan came from gd-sim and the real player did not end up where the
    // simulator said it would, the model is wrong here. Rather than let that
    // drift compound, flag the region so future plans go to the learner, and
    // re-seed the simulator from the real state.
    if (m_planSource == Source::Pathfinder && m_arbiter) {
        // The arbiter keeps its own prediction from the last plan it issued.
        m_arbiter->reseedFrom(exitState);
    }

    // --- plant the next checkpoint --------------------------------------
    bool const canPlant =
        !m_config.checkpointOnReleaseOnly || allButtonsReleased();

    if (canPlant && m_ledger.depth() < m_config.maxCheckpoints) {
        if (plantCheckpoint(layer)) {
            Anchor next;
            next.checkpoint = static_cast<CheckpointObject*>(
                layer->m_checkpointArray && layer->m_checkpointArray->count()
                    ? layer->m_checkpointArray->lastObject()
                    : nullptr);
            next.state = exitState;
            next.frame = frame;
            next.macroLength = m_macro.size();
            for (int p = 0; p < 2; ++p) {
                for (int b = 0; b < 4; ++b) next.held[p][b] = m_driven[p][b];
            }
            m_ledger.push(next);
        }
    } else if (!canPlant) {
        // Mid-hold: do not anchor here. Keep running and let the next commit
        // opportunity land on a released frame.
        (void)macroBefore;
    }

    m_phase = Phase::Planning;
}

void PracticeDirector::failAttempt(PlayLayer* layer, Frame deathFrame) {
    auto& anchor = m_ledger.top();
    ++anchor.failures;

    m_memory.recordDeath(deathFrame);
    ++m_dirtyWrites;

    if (m_arbiter) {
        m_arbiter->reportFailure(anchor.state, deathFrame, m_planSource);
    }

    // A segment recalled from memory that just killed us is worse than nothing.
    if (m_planFromMemory) {
        m_memory.forget(anchor.state.hash(), "remembered segment no longer works");
        m_planFromMemory = false;
    }

    // Roll back the macro to this anchor: everything after it was speculative.
    m_macro.truncate(anchor.macroLength);

    if (anchor.failures >= m_config.retryBudget) {
        rollback(layer, "retry budget exhausted at this anchor");
    } else {
        m_phase = Phase::Planning;
    }
}

void PracticeDirector::rollback(PlayLayer* layer, char const* reason) {
    if (!layer) return;

    ++m_rollbacks;

    if (m_ledger.atSpawn()) {
        // Nothing left to drop. Widen the search from the very start instead of
        // giving up: the opening of the level is genuinely the hardest place to
        // be stuck, but it is also the cheapest to re-search.
        auto& anchor = m_ledger.top();
        anchor.failures = 0;
        ++anchor.horizonBoost;
        anchor.simDistrusted = true;
        m_macro.truncate(anchor.macroLength);
        log::info("GDUBAI: rollback at spawn ({}), horizon boost -> {}", reason,
                  anchor.horizonBoost);
        m_phase = Phase::Planning;
        return;
    }

    // Drop the last checkpoint in GD and in our ledger, in that order.
    //
    // removeCheckpoint(false) removes the LAST checkpoint. This is not a guess:
    // PlayerObject::removePlacedCheckpoint()'s reconstructed body is
    //     GameManager::sharedState()->m_playLayer->removeCheckpoint(false);
    // used to undo the checkpoint that was just placed.
    dropCheckpoint(layer);
    m_ledger.pop();

    auto& anchor = m_ledger.top();
    anchor.failures = 0;
    ++anchor.horizonBoost;   // search further from here than we did last time
    anchor.simDistrusted = true;

    m_macro.truncate(anchor.macroLength);

    if (m_arbiter) m_arbiter->distrustSimAround(anchor.frame);

    log::info(
        "GDUBAI: rollback #{} ({}) -> {} checkpoint(s) left, macro rewound to {} "
        "event(s), horizon boost {}",
        m_rollbacks, reason, m_ledger.depth() - 1, m_macro.size(), anchor.horizonBoost);

    m_phase = Phase::RollingBack;

    // resetLevel() respawns at whatever is now the last checkpoint; onLevelReset
    // picks it up from there and returns us to Planning.
    layer->resetLevel();
}

void PracticeDirector::beginValidation(PlayLayer* layer) {
    if (!layer) return;

    log::info("GDUBAI: practice solve complete ({} event(s)). Validating in normal mode.",
              m_macro.size());

    releaseAllButtons(layer);
    layer->removeAllCheckpoints();
    layer->togglePracticeMode(false);

    m_validating = true;
    m_validateCursor = 0;
    m_phase = Phase::Validating;

    FrameClock::get().reset();
    layer->resetLevelFromStart();
}

void PracticeDirector::finish(PlayLayer* layer, bool solved, char const* reason) {
    m_macro.reachedPercent = std::max(m_macro.reachedPercent, m_bestPercent);
    m_memory.offerBest(m_macro);
    flushMemory(true);

    m_phase = solved ? Phase::Finished : Phase::Failed;
    m_note = reason ? reason : "";

    log::info("GDUBAI: run finished - {} ({}), {:.2f}%, {} rollback(s), {} attempt(s)",
              solved ? "SOLVED" : "stopped", reason, m_macro.reachedPercent, m_rollbacks,
              m_attempts);

    if (layer) releaseAllButtons(layer);
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

bool PracticeDirector::plantCheckpoint(PlayLayer* layer) {
    if (!layer) return false;

    unsigned const before = layer->m_checkpointArray ? layer->m_checkpointArray->count() : 0;

    // markCheckpoint() creates the CheckpointObject, fills it from the current
    // game state and stores it in m_checkpointArray. It is addressed on every
    // platform, so this is a plain call.
    layer->markCheckpoint();

    unsigned const after = layer->m_checkpointArray ? layer->m_checkpointArray->count() : 0;
    if (after != before + 1) {
        log::warn("GDUBAI: markCheckpoint did not add a checkpoint ({} -> {}). "
                  "Are checkpoints disabled in the game options?", before, after);
        return false;
    }
    return true;
}

void PracticeDirector::dropCheckpoint(PlayLayer* layer) {
    if (!layer) return;
    unsigned const before = layer->m_checkpointArray ? layer->m_checkpointArray->count() : 0;
    if (before == 0) return;

    layer->removeCheckpoint(false);

    unsigned const after = layer->m_checkpointArray ? layer->m_checkpointArray->count() : 0;
    if (after != before - 1) {
        log::warn("GDUBAI: removeCheckpoint(false) left {} checkpoint(s), expected {}",
                  after, before - 1);
    }
}

void PracticeDirector::reapplyHeldButtons(PlayLayer* layer, Anchor const& anchor) {
    if (!layer) return;
    for (int p = 0; p < 2; ++p) {
        for (int b = kButtonJump; b <= kButtonRight; ++b) {
            if (!anchor.held[p][b]) continue;
            m_driven[p][b] = true;
            pressButton(layer, true, b, p == 1);
        }
    }
}

void PracticeDirector::releaseAllButtons(PlayLayer* layer) {
    if (!layer) return;
    for (int p = 0; p < 2; ++p) {
        for (int b = kButtonJump; b <= kButtonRight; ++b) {
            if (!m_driven[p][b]) continue;
            m_driven[p][b] = false;
            pressButton(layer, false, b, p == 1);
        }
    }
}

bool PracticeDirector::allButtonsReleased() const {
    for (auto const& player : m_driven) {
        for (int b = kButtonJump; b <= kButtonRight; ++b) {
            if (player[b]) return false;
        }
    }
    return true;
}

void PracticeDirector::flushMemory(bool force) {
    using namespace std::chrono;
    auto const now = steady_clock::now();

    // Committing happens several times a second at high speed; writing the JSON
    // every time would dominate the frame budget. Debounce to at most one write
    // every few seconds unless forced.
    if (!force) {
        if (m_dirtyWrites == 0) return;
        if (duration<double>(now - m_lastFlush).count() < 5.0) return;
    }
    if (m_dirtyWrites == 0 && !force) return;

    m_memory.save();
    m_dirtyWrites = 0;
    m_lastFlush = now;
}

// ---------------------------------------------------------------------------
// reporting
// ---------------------------------------------------------------------------

std::string PracticeDirector::statusLine() const {
    char const* phase = "idle";
    switch (m_phase) {
        case Phase::Preparing:   phase = "preparing"; break;
        case Phase::Planning:    phase = "planning"; break;
        case Phase::Running:     phase = "running"; break;
        case Phase::RollingBack: phase = "rewinding"; break;
        case Phase::Validating:  phase = "validating"; break;
        case Phase::Finished:    phase = "solved"; break;
        case Phase::Failed:      phase = "stopped"; break;
        default: break;
    }

    return fmt::format(
        "GDUBAI {} | {:.2f}% | {} cp | {} ev | {} rollback(s)",
        phase, m_bestPercent, m_ledger.depth() > 0 ? m_ledger.depth() - 1 : 0,
        m_macro.size(), m_rollbacks);
}

std::string PracticeDirector::detailLine() const {
    auto const& anchor = m_ledger.top();
    return fmt::format(
        "{} via {} | fails {}/{} | known {} f | {}",
        anchor.state.describe(), sourceName(m_planSource), anchor.failures,
        m_config.retryBudget, m_memory.knownFrames(),
        m_rng.deterministic() ? "deterministic" : "RNG level");
}

}  // namespace gdubai
