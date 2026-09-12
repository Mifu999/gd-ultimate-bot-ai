#include "Arbiter.hpp"

#include "../learn/Sequence.hpp"

#include <Geode/Geode.hpp>

#include <Level.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_set>

using namespace geode::prelude;

namespace gdubai {

namespace {

// One candidate line in the local beam search.
struct BeamNode {
    Level sim;
    std::vector<InputEvent> events;
    bool held = false;
    double score = 0.0;
    float minClearance = std::numeric_limits<float>::infinity();

    explicit BeamNode(Level const& source) : sim(source) {}
};

// Quantises a simulated player state so two beam nodes that are physically the
// same get merged instead of both being expanded. Without this the beam fills up
// with near-duplicates and the effective width collapses.
std::uint64_t mergeKey(Player const& player, bool held) {
    auto q = [](double value, double step) {
        return static_cast<std::int64_t>(std::llround(value / step));
    };
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xff;
            h *= 1099511628211ull;
        }
    };
    mix(static_cast<std::uint64_t>(q(player.pos.x, 0.05)));
    mix(static_cast<std::uint64_t>(q(player.pos.y, 0.05)));
    mix(static_cast<std::uint64_t>(q(player.velocity, 0.05)));
    mix(static_cast<std::uint64_t>(static_cast<int>(player.vehicle.type)) |
        (static_cast<std::uint64_t>(player.upsideDown) << 8) |
        (static_cast<std::uint64_t>(player.small) << 9) |
        (static_cast<std::uint64_t>(held) << 10));
    return h;
}

}  // namespace

// ---------------------------------------------------------------------------

struct Arbiter::Impl {
    // The mirror. Kept advanced along exactly the inputs that have been
    // committed to the real macro, so rollback(anchorFrame) lands on the state
    // the real game is in at that anchor.
    std::unique_ptr<Level> sim;
    float endX = 0.f;
    Frame simFrame = 0;

    // Regions where gd-sim proved untrustworthy. Plans in these ranges skip the
    // simulator entirely.
    std::unordered_set<Frame> distrustedBuckets;

    // Climber state: the tape the learner mutates, shared with whatever the
    // simulator has already proven so the learner never re-searches it.
    std::vector<int> lockedTape;
    SequenceParams sequenceParams;
    std::mt19937 rng{std::random_device{}()};
    std::uint32_t stuck = 0;

    // The last prediction handed out, used for divergence measurement.
    bool hasPrediction = false;
    double predX = 0.0;
    double predY = 0.0;
    double predVy = 0.0;

    static constexpr Frame kDistrustBucket = 240;
};

Arbiter::Arbiter() : m_impl(std::make_unique<Impl>()) {}
Arbiter::~Arbiter() = default;

bool Arbiter::prepare(std::string const& levelString, float knownEndX) {
    m_impl->sim.reset();
    m_impl->simFrame = 0;
    m_impl->distrustedBuckets.clear();
    m_impl->lockedTape.clear();
    m_impl->stuck = 0;
    m_simReady = false;
    m_note.clear();

    if (levelString.empty()) {
        m_note = "empty level string";
        return false;
    }

    try {
        m_impl->sim = std::make_unique<Level>(levelString);
    } catch (std::exception const& e) {
        m_note = std::string("gd-sim threw: ") + e.what();
        log::warn("GDUBAI: {}", m_note);
        return false;
    } catch (...) {
        m_note = "gd-sim threw an unknown exception";
        return false;
    }

    // PlayLayer::getEndPosition() is authoritative for the playable endpoint;
    // the simulator's own inferred length is only a fallback.
    if (knownEndX > 1.f) {
        m_impl->sim->length = knownEndX;
        m_impl->sim->lengthSource = "PlayLayer::getEndPosition";
    }
    m_impl->endX = m_impl->sim->length;

    if (!m_impl->sim->unsupportedObjects.empty()) {
        m_note = fmt::format("{} object(s) gd-sim does not model",
                             m_impl->sim->unsupportedObjects.size());
        log::info("GDUBAI: {} - the learner will cover those regions", m_note);
    }

    m_simReady = true;
    return true;
}

void Arbiter::distrustSimAround(Frame frame) {
    m_impl->distrustedBuckets.insert(frame / Impl::kDistrustBucket);
}

bool Arbiter::simTrustedAt(Frame frame) const {
    if (!m_simReady) return false;
    return !m_impl->distrustedBuckets.contains(frame / Impl::kDistrustBucket);
}

void Arbiter::reseedFrom(AnchorState const& anchor) {
    if (!m_simReady || !m_impl->sim) return;
    if (!m_impl->hasPrediction) return;

    double const dx = anchor.x - m_impl->predX;
    double const dy = anchor.y - m_impl->predY;
    double const error = std::sqrt(dx * dx + dy * dy);

    // gd-sim cannot be teleported to an arbitrary state without violating its
    // own invariants - it is a forward simulator with a history, not a state
    // container. So "reseeding" here means measuring the disagreement and,
    // where it is too large, refusing to trust the model in this region rather
    // than pretending we corrected it.
    if (error > 4.0) {
        log::warn(
            "GDUBAI: gd-sim diverged from the game at frame {} by {:.2f} units "
            "(sim {:.1f},{:.1f} vs real {:.1f},{:.1f}). Handing this region to "
            "the learner.",
            anchor.frame, error, m_impl->predX, m_impl->predY, anchor.x, anchor.y);
        distrustSimAround(anchor.frame);
    }
    m_impl->hasPrediction = false;
}

// ---------------------------------------------------------------------------

Plan Arbiter::plan(PlanRequest const& request, MacroMemory const& memory) {
    Plan plan;
    plan.start = request.anchor.frame;

    // --- 1. memory ------------------------------------------------------
    //
    // A stretch solved before, from a state that hashes identically, is replayed
    // verbatim. This is the whole point of the memory: on a second run the
    // director walks straight through everything it already knows.
    if (auto remembered = memory.recall(request.anchor.hash())) {
        // Only trust it on the first attempt at this anchor. If we already
        // failed here, the remembered line is suspect and we search instead.
        if (request.attemptIndex == 0 && !remembered->events.empty()) {
            plan.events = remembered->events;

            // Rebase onto the current frame: the same physical situation can
            // legitimately occur at a different time (after a rollback, for
            // instance), and the stored frames are absolute.
            if (remembered->startFrame != request.anchor.frame) {
                std::int64_t const shift =
                    static_cast<std::int64_t>(request.anchor.frame) -
                    static_cast<std::int64_t>(remembered->startFrame);
                for (auto& event : plan.events) {
                    std::int64_t rebased = static_cast<std::int64_t>(event.frame) + shift;
                    event.frame = static_cast<Frame>(std::max<std::int64_t>(0, rebased));
                }
            }

            plan.source = Source::Memory;
            plan.end = request.anchor.frame + remembered->length();
            plan.note = "recalled from memory";
            ++m_memoryHits;
            return plan;
        }
    }

    // --- 2. simulator ---------------------------------------------------
    bool const useSim = m_simReady && m_impl->sim &&
                        !request.simDistrusted &&
                        simTrustedAt(request.anchor.frame);

    if (useSim) {
        plan = planWithSimulator(request);
        if (plan.usable()) {
            ++m_pathfinderPlans;
            return plan;
        }
        // The simulator had the chance and produced nothing survivable. That is
        // itself information: stop asking it here.
        distrustSimAround(request.anchor.frame);
    }

    // --- 3. learner -----------------------------------------------------
    plan = planWithLearner(request);
    if (plan.usable()) ++m_learnerPlans;
    return plan;
}

Plan Arbiter::planWithSimulator(PlanRequest const& request) {
    Plan plan;
    plan.start = request.anchor.frame;
    plan.source = Source::Pathfinder;

    auto& sim = *m_impl->sim;

    // Put the mirror back to the anchor. This only works because the mirror has
    // been advanced along exactly the committed inputs - see advanceMirror().
    if (static_cast<Frame>(sim.currentFrame()) < request.anchor.frame) {
        // We are behind the anchor: the mirror was never advanced this far
        // (typically because the learner produced the inputs that got us here
        // and the simulator disagrees about them). Refuse rather than plan from
        // a state that is not the player's.
        return plan;
    }
    sim.rollback(static_cast<int>(request.anchor.frame));

    // Sanity: does the mirror actually agree with where the player is?
    auto const& seed = sim.latestState();
    double const dx = seed.pos.x - request.anchor.x;
    double const dy = seed.pos.y - request.anchor.y;
    if (std::sqrt(dx * dx + dy * dy) > 6.0) {
        log::debug("GDUBAI: mirror disagrees with the game at frame {} by {:.1f} units",
                   request.anchor.frame, std::sqrt(dx * dx + dy * dy));
        return plan;
    }

    // --- receding-horizon beam search -----------------------------------
    //
    // At every frame each surviving line branches into "hold" and "release".
    // Lines that die are dropped, physically identical lines are merged, and
    // only the best `beamWidth` survive to the next frame. We then commit a
    // short prefix - long enough to be worth the search, short enough that the
    // next plan can react to what actually happened.
    int const beamWidth = request.deathsNearby > 20 ? 96 : 48;
    Frame const horizon = request.horizon;
    Frame const commit = std::min<Frame>(horizon, 120);

    std::vector<BeamNode> beam;
    beam.emplace_back(sim);

    auto const deadline =
        std::chrono::steady_clock::now() +
        std::chrono::duration<double>(request.budgetSeconds);

    bool reachedEnd = false;

    for (Frame step = 0; step < horizon; ++step) {
        if (std::chrono::steady_clock::now() > deadline) break;

        std::vector<BeamNode> next;
        next.reserve(beam.size() * 2);
        std::unordered_set<std::uint64_t> seen;

        for (auto const& node : beam) {
            for (bool hold : {false, true}) {
                BeamNode child(node.sim);
                child.events = node.events;
                child.held = hold;
                child.minClearance = node.minClearance;

                auto& state = child.sim.runFrame(hold, 1.f / static_cast<float>(kTPS));
                if (state.dead) continue;

                if (hold != node.held) {
                    child.events.push_back(InputEvent{
                        request.anchor.frame + step,
                        static_cast<std::uint8_t>(kButtonJump),
                        false,
                        hold,
                    });
                }

                if (state.completed) {
                    reachedEnd = true;
                }

                auto const key = mergeKey(state, hold);
                if (!seen.insert(key).second) continue;

                // Score: get as far right as possible, prefer fewer input
                // changes (cleaner macros replay more reliably), and prefer
                // lines that did not skim an edge.
                child.score = state.pos.x * 10.0 - child.events.size() * 0.15;
                next.push_back(std::move(child));
            }
        }

        if (next.empty()) {
            // Everything died within the horizon. Nothing usable from here.
            return plan;
        }

        std::partial_sort(
            next.begin(),
            next.begin() + std::min<std::size_t>(next.size(), beamWidth),
            next.end(),
            [](BeamNode const& a, BeamNode const& b) { return a.score > b.score; });
        if (next.size() > static_cast<std::size_t>(beamWidth)) {
            next.resize(beamWidth);
        }
        beam = std::move(next);

        if (reachedEnd) break;
    }

    if (beam.empty()) return plan;

    auto const& best = beam.front();

    Frame const planLength = reachedEnd ? horizon : commit;
    plan.end = request.anchor.frame + planLength;

    for (auto const& event : best.events) {
        if (event.frame < plan.end) plan.events.push_back(event);
    }

    // Hand back the predicted landing state so the director can measure
    // divergence once the plan has been played for real.
    auto const& predicted = best.sim.latestState();
    plan.hasPrediction = true;
    plan.predictedX = predicted.pos.x;
    plan.predictedY = predicted.pos.y;
    plan.predictedYVelocity = predicted.velocity;

    m_impl->hasPrediction = true;
    m_impl->predX = predicted.pos.x;
    m_impl->predY = predicted.pos.y;
    m_impl->predVy = predicted.velocity;

    plan.note = reachedEnd ? "simulator reached the end" : "simulator horizon";
    return plan;
}

Plan Arbiter::planWithLearner(PlanRequest const& request) {
    Plan plan;
    plan.start = request.anchor.frame;
    plan.source = Source::Climber;

    // The climber works on a "tape": a sorted list of step indices at which the
    // jump input flips. buildCandidate keeps the locked prefix verbatim, mutates
    // the region just before the frontier, and generates fresh inputs beyond it.
    //
    // The locked prefix is fed by the simulator's successes (see
    // reportSuccess), which is the concrete half of "the two engines help each
    // other": the learner never re-searches ground the simulator proved.
    int const frontier = static_cast<int>(request.anchor.frame);
    int const horizon = frontier + static_cast<int>(request.horizon);
    int const lookback = climberBacktrack(
        m_impl->sequenceParams, static_cast<int>(m_impl->stuck), frontier, m_impl->rng);

    auto tape = buildCandidate(
        m_impl->lockedTape, frontier, lookback, horizon, m_impl->sequenceParams,
        m_impl->rng, static_cast<int>(m_impl->stuck));

    bool held = false;
    for (int step : tape) {
        if (step < frontier) {
            held = !held;   // replay the locked prefix to recover the hold state
            continue;
        }
        if (step >= horizon) break;
        held = !held;
        plan.events.push_back(InputEvent{
            static_cast<Frame>(step),
            static_cast<std::uint8_t>(kButtonJump),
            false,
            held,
        });
    }

    plan.end = static_cast<Frame>(horizon);
    plan.note = fmt::format("climber tape, lookback {}, stuck {}", lookback, m_impl->stuck);
    return plan;
}

// ---------------------------------------------------------------------------

void Arbiter::reportSuccess(Segment const& segment) {
    m_impl->stuck = 0;

    // Feed the committed stretch into the learner's locked tape so it treats
    // this ground as solved. Toggles are stored as absolute frame indices, the
    // same representation buildCandidate expects.
    for (auto const& event : segment.events) {
        if (event.button != kButtonJump || event.player2) continue;
        m_impl->lockedTape.push_back(static_cast<int>(event.frame));
    }
    std::sort(m_impl->lockedTape.begin(), m_impl->lockedTape.end());
    m_impl->lockedTape.erase(
        std::unique(m_impl->lockedTape.begin(), m_impl->lockedTape.end()),
        m_impl->lockedTape.end());

    // Advance the mirror along the inputs that were actually played, so the
    // next rollback(anchorFrame) lands on the right state.
    advanceMirror(segment);
}

void Arbiter::advanceMirror(Segment const& segment) {
    if (!m_simReady || !m_impl->sim) return;

    auto& sim = *m_impl->sim;
    if (static_cast<Frame>(sim.currentFrame()) > segment.startFrame) {
        sim.rollback(static_cast<int>(segment.startFrame));
    }
    if (static_cast<Frame>(sim.currentFrame()) != segment.startFrame) {
        // The mirror is behind; it cannot be fast-forwarded without the inputs
        // that got us here, which the learner may not have shared. Leave it and
        // let planWithSimulator refuse next time.
        return;
    }

    std::size_t cursor = 0;
    bool held = false;
    for (Frame frame = segment.startFrame; frame < segment.endFrame; ++frame) {
        while (cursor < segment.events.size() && segment.events[cursor].frame <= frame) {
            auto const& event = segment.events[cursor++];
            if (event.button == kButtonJump && !event.player2) held = event.down;
        }
        auto& state = sim.runFrame(held, 1.f / static_cast<float>(kTPS));
        if (state.dead) {
            // The mirror died where the real game survived: a hard divergence.
            distrustSimAround(frame);
            log::warn("GDUBAI: mirror died at frame {} where the game did not.", frame);
            return;
        }
    }
    m_impl->simFrame = segment.endFrame;
}

void Arbiter::reportFailure(AnchorState const& anchor, Frame deathFrame, Source source) {
    ++m_impl->stuck;
    if (source == Source::Pathfinder) {
        // The simulator promised a line and it killed us for real.
        distrustSimAround(deathFrame);
    }
    (void)anchor;
}

void Arbiter::reset() {
    m_impl->sim.reset();
    m_impl->distrustedBuckets.clear();
    m_impl->lockedTape.clear();
    m_impl->stuck = 0;
    m_impl->hasPrediction = false;
    m_simReady = false;
    m_pathfinderPlans = 0;
    m_learnerPlans = 0;
    m_memoryHits = 0;
}

}  // namespace gdubai
