#pragma once

#include "../core/MacroMemory.hpp"
#include "../core/Types.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace gdubai {

// What the director asks for at an anchor.
struct PlanRequest {
    AnchorState anchor;
    Frame horizon = 480;
    std::uint32_t attemptIndex = 0;   // failures already spent at this anchor
    int deathsNearby = 0;             // from MacroMemory, across all past runs
    bool simDistrusted = false;       // gd-sim was wrong here before
    double budgetSeconds = 4.0;
};

// What comes back.
struct Plan {
    std::vector<InputEvent> events;   // absolute frames, starting at anchor.frame
    Source source = Source::Unknown;
    Frame start = 0;
    Frame end = 0;

    // gd-sim's prediction of where the player will be at `end`, when the plan
    // came from the simulator. The director compares this against reality at
    // commit time; a mismatch is how sim/real divergence gets detected instead
    // of quietly producing garbage plans forever.
    bool hasPrediction = false;
    double predictedX = 0.0;
    double predictedY = 0.0;
    double predictedYVelocity = 0.0;

    std::string note;

    bool usable() const { return end > start; }
};

// Chooses between the two engines and keeps them fed with each other's results.
//
// The cooperation is concrete, not decorative:
//
//   Pathfinder -> learner   every committed pathfinder stretch is pushed into
//                           the climber's tape as a locked prefix, so the
//                           learner never re-searches ground the simulator has
//                           already proved.
//
//   learner -> Pathfinder   when the learner clears a wall the simulator could
//                           not (an object gd-sim does not model, or a spot
//                           where sim and reality diverged), the director
//                           re-seeds the simulator from the REAL game state at
//                           the new checkpoint. That resynchronises the model
//                           with the game instead of letting it drift.
//
//   shared death map        both read MacroMemory's histogram, so a spot that
//                           killed previous runs gets a longer horizon and a
//                           bigger budget before the first failure, not after
//                           the twentieth.
class Arbiter {
public:
    Arbiter();
    ~Arbiter();

    // Builds the offline simulator from a decompressed level string. Returns
    // false if gd-sim could not parse it, in which case the director falls back
    // to learner-only mode (which still works - it just searches blind).
    bool prepare(std::string const& levelString, float knownEndX);

    bool simulatorReady() const { return m_simReady; }
    std::string const& lastNote() const { return m_note; }

    // Produces the next plan. Consults memory first, then the simulator, then
    // the learner, following the rules described above.
    Plan plan(PlanRequest const& request, MacroMemory const& memory);

    // Feedback after the plan was run for real.
    void reportSuccess(Segment const& segment);
    void reportFailure(AnchorState const& anchor, Frame deathFrame, Source source);

    // Forces the simulator's notion of "now" back onto the real game state.
    // Called after every commit so drift cannot accumulate across a long run.
    void reseedFrom(AnchorState const& anchor);

    // Marks the region around a frame as one the simulator cannot be trusted
    // for, which routes future plans there straight to the learner.
    void distrustSimAround(Frame frame);
    bool simTrustedAt(Frame frame) const;

    void reset();

    // Counters for the HUD.
    std::uint64_t pathfinderPlans() const { return m_pathfinderPlans; }
    std::uint64_t learnerPlans() const { return m_learnerPlans; }
    std::uint64_t memoryHits() const { return m_memoryHits; }

private:
    // Local receding-horizon beam search on the gd-sim mirror.
    Plan planWithSimulator(PlanRequest const& request);
    // Tape mutation around the frontier, seeded with whatever the simulator has
    // already locked in.
    Plan planWithLearner(PlanRequest const& request);
    // Steps the mirror along inputs that were actually played, so a later
    // rollback(frame) lands on the state the real game was in.
    void advanceMirror(Segment const& segment);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_simReady = false;
    std::string m_note;
    std::uint64_t m_pathfinderPlans = 0;
    std::uint64_t m_learnerPlans = 0;
    std::uint64_t m_memoryHits = 0;
};

}  // namespace gdubai
