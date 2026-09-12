# GD Ultimate Bot AI — architecture

This document records *why* the mod is built the way it is, and which claims in
it are verified against a source rather than assumed. Anything below marked with
a source can be re-checked; anything marked **unverified** has not been, and is
treated as a risk in the code rather than as a fact.

---

## 1. What the mod is

Two engines that were separate mods, driving one player:

| | Comes from | What it is good at |
|---|---|---|
| **Pathfinder** | `candyzp/pathfinder` (fork of `camila314/pathfinder`) | Exact. It simulates GD's physics offline at 240 TPS and finds frame-perfect lines — when the objects in front of it are ones `gd-sim` models. |
| **Learner** | `NEATGD` (Mifu) | Blind but general. It does not need to understand an object to get past it, because it plays the real game and mutates what it tried. |

The claim in `infos.txt` was "a bot and an AI that help each other". That is
implemented as three concrete mechanisms, not as a slogan — see §5.

On top of both sits the thing this version was actually built for: a
**practice-mode director** that solves a level incrementally, anchored on GD's
own checkpoints, and never re-solves ground it has already cleared.

---

## 2. The practice-mode director

### 2.1 The loop

```
PREPARE     force practice mode on, removeAllCheckpoints(), load memory,
            scan the level for RNG triggers, build the gd-sim mirror

PLAN(A)     ask the Arbiter for inputs covering [A.frame, A.frame + horizon]
RUN         feed them through handleButton on processCommands

  survived commitStride frames past A?
    COMMIT    append the played inputs to the macro
              markCheckpoint()  ->  push a new anchor onto the ledger
              store the stretch in MacroMemory
              re-measure gd-sim against reality
              PLAN(new anchor)

  died?
    FAIL      deaths[frame]++, truncate the macro back to A.macroLength
              attempts < retryBudget ?  PLAN(A) again with a different candidate
                                     :  ROLLBACK

ROLLBACK    removeCheckpoint(false)      <- drops the LAST checkpoint
            pop the ledger, truncate the macro to the new top's offset
            grant the new anchor a longer horizon, mark gd-sim distrusted here
            resetLevel()  -> the game respawns us at the new last checkpoint

VALIDATE    at 100%: leave practice mode, clear checkpoints, replay the whole
            macro from frame 0 in NORMAL mode. Only then is it CERTIFIED.
```

That is exactly the behaviour asked for: checkpoints all the time, cleared
sections never replayed, and when a spot blocks, checkpoints are removed one at
a time until the player is unstuck.

### 2.2 Why anchor on GD's own checkpoints

A `CheckpointObject` is not a position — it is a full world snapshot. Its fields
(`GeometryDash.bro:1888`):

```
GJGameState  m_gameState
GJShaderState m_shaderState
FMODAudioState m_audioState
GameObject*  m_physicalCheckpointObject
PlayerCheckpoint* m_player1Checkpoint / m_player2Checkpoint
gd::vector<SavedObjectStateRef>      m_vectorSavedObjectStateRef
gd::vector<SavedActiveObjectState>   m_vectorActiveSaveObjectState
gd::vector<SavedSpecialObjectState>  m_vectorSpecialSaveObjectState
EffectManagerState                   m_effectManagerState
gd::unordered_map<int,SequenceTriggerState> m_sequenceTriggerStateUnorderedMap
```

Nothing we could build would restore trigger state, effect-manager state and
saved object state as faithfully as the game restoring its own. So the director
does not invent a resume mechanism; it uses the one already in the binary.

### 2.3 The API it drives

All verified hookable/callable on Windows in the 2.2081 bindings:

| Call | Windows address | Note |
|---|---|---|
| `PlayLayer::togglePracticeMode(bool)` | `0x3b9e50` | |
| `PlayLayer::markCheckpoint()` | `0x3b7570` | creates, fills and stores in one go |
| `PlayLayer::removeCheckpoint(bool first)` | `0x3b7f00` | see below |
| `PlayLayer::removeAllCheckpoints()` | `0x3b8040` | |
| `PlayLayer::loadFromCheckpoint(CheckpointObject*)` | `0x3b7640` | |
| `PlayLayer::resetLevel()` | `0x3b8eb0` | respawns at the last checkpoint in practice |
| `GJBaseGameLayer::handleButton(bool, int, bool)` | `0x2338e0` | |

**`removeCheckpoint(false)` removes the LAST checkpoint.** Not a guess — the
reconstructed inline body of `PlayerObject::removePlacedCheckpoint()` is:

```cpp
void PlayerObject::removePlacedCheckpoint() {
    if (m_checkpointTimeout) {
        GameManager::sharedState()->m_playLayer->removeCheckpoint(false);
        m_checkpointTimeout = false;
    }
}
```

i.e. `false` is what the game itself passes to undo the checkpoint just placed.

**Do not hook these** — they are `win inline`, so there is no address to hook on
Windows and `$modify` fails with a `static_assert`. Calling them is fine, because
the codegen emits reconstructed bodies:

`getLastCheckpoint()`, `loadLastCheckpoint()`, `queueCheckpoint()`,
`checkpointWithID(int)`, `getEndPosition()`.

The mod calls `getEndPosition()` (it is the authoritative playable endpoint) and
reads `m_checkpointArray` directly instead of `getLastCheckpoint()`.

---

## 3. Three traps that shaped the code

### 3.1 A checkpoint does not save your button state

`PlayerCheckpoint` has roughly 110 fields — position, last position, y velocity,
every gamemode flag, mini, gravity mod, slope state, dash state, four collision
logs, the object you are snapped to. There is **no field for "a button is
currently held"**. `m_jumpBuffered` exists; a held button does not.

So on respawn, every button comes back released. If a solved stretch was
mid-hold when the checkpoint landed, the hold is silently lost on replay.

This is a known trap outside this project too — xdBot's changelog carries
"Fixed macro not recording a release when you place a checkpoint while holding."

Two mitigations, both implemented:

- `checkpoint-on-release-only` (default **on**): only anchor on frames where
  every button is released. Removes the entire problem class.
- Otherwise, `Anchor::held[2][4]` records the state and
  `reapplyHeldButtons()` re-presses after `resetLevel()`.

### 3.2 Frame counting

The GDReplayFormat spec is explicit: *"Use a frame counter to store frames.
Multiplying time can lead to inaccuracy in frame counting."*

And `PlayLayer::m_attemptTime` is not restored by a checkpoint anyway.
`GJGameState::m_levelTime` is (it lives inside the stored `GJGameState`), but it
is still a double.

So `FrameClock` keeps an integer counter, incremented once per
`processCommands`, and **restores it from the ledger anchor on every respawn**.
`FrameClock::drift()` compares it to `m_levelTime * 240` and logs a warning past
four frames — a cheap alarm for the case where the counter and the game's own
restored clock disagree, which would mean the macro is being written against the
wrong timeline.

### 3.3 `handleButton`'s third parameter

The bindings name it `isPlayer1`. The Pathfinder fork calls it as
`handleButton(down, button, input.player2)` — passing player2 where player1 is
expected. xdBot, the reference macro bot on 2.2081, calls it as
`handleButton(hold, button, !player2)`.

**This mod follows xdBot.** The fork's convention is harmless on single-player
levels and inverts the players on every dual level.

---

## 4. Randomness: the honest limit

From the Library of Geometria's RNG page:

> There's 1 random seed per level that is generated randomly on each attempt
> (the seed stays the same when restarting from checkpoint).

Two consequences, pulling in opposite directions:

**Good for the director.** Rolling back to a checkpoint does not reshuffle the
level underneath it. A stretch that worked once behaves the same way on the next
rollback within the same attempt. The incremental approach is sound.

**Bad for the macro.** A *full* restart draws a new seed. On a level that
consumes it, a finished macro is not guaranteed to reproduce — and no amount of
cleverness on our side changes that, because it is a property of the game.

Four triggers consume the seed (same source): Random, Advanced Random, Advanced
Follow, Edit Advanced Follow. Their IDs, from the gd-info-explorer object table:

```
1912  Random Trigger
2068  Advanced Random Trigger
3016  Advanced Follow Trigger
3660  Edit Advanced Follow Trigger
```

`RngScan` counts them in one linear pass over the level string and the mod says
so up front, rather than promising a determinism that does not exist.
(3661 Re-Target Advanced Follow is deliberately excluded — the source does not
list it as calling `rand()`, and a false alarm is worse than no alarm.)

GD's `rand()` is C++'s, with `RAND_MAX = 32767`.

### 4.1 And the wider point: validation is mandatory

Practice-mode restore is thorough but **not provably exact**. Two things are
known not to be inside the stored `GJGameState`: `m_randomSeed` and
`m_replayRandSeed` live on `GJBaseGameLayer`, not on `GJGameState`.
Whether they survive a checkpoint restore is **unverified** — the RNG page says
the seed persists across a checkpoint restart, which is consistent with them
being restored or with them simply never being touched, and we did not confirm
which.

Rather than reason about it, the director just checks: `validate-in-normal-mode`
replays the finished macro from frame 0 with practice mode off and checkpoints
cleared. A macro that survives that is `CERTIFIED`. One that does not is
reported as diverging at a specific frame, which is actionable.

This is the single most important design decision in the mod. "It worked in
practice" and "this macro plays the level" are different claims.

---

## 5. How the two engines actually cooperate

Three mechanisms, all in `Arbiter`:

**Pathfinder → learner.** Every committed stretch is pushed into the climber's
locked tape (`reportSuccess`). `buildCandidate` keeps the locked prefix verbatim
and only mutates around the frontier, so the learner never re-searches ground the
simulator already proved.

**Learner → Pathfinder.** The arbiter keeps a `Level` *mirror* advanced along
exactly the inputs that were really played (`advanceMirror`). When the learner
clears something the simulator could not, the mirror is stepped through the
learner's inputs too — so a later `rollback(anchorFrame)` still lands on the real
state. If the mirror dies where the real game survived, that region is marked
distrusted and future plans there skip the simulator.

**Shared death map.** Both read `MacroMemory`'s frame→count histogram,
accumulated across every run ever made on the level. A spot that killed previous
runs gets a wider beam (96 instead of 48) and a longer horizon *before* the first
failure, not after the twentieth.

### 5.1 What `reseedFrom` does and does not do

It **measures** the gap between gd-sim's prediction and where the player actually
ended up, and marks the region distrusted past 4 units. It does **not** inject
the real state into the simulator: `Level` is a forward simulator with a history,
not a state container, and teleporting it would violate its own invariants.

A true re-synchronisation would mean patching `gd-sim` to accept an arbitrary
initial `Player`. That is feasible and would strictly improve the Pathfinder
side after a learner-solved section, but it is a separate piece of work and
pretending otherwise would be dishonest about what the code does today.

---

## 6. The memory

One JSON file per level, under `<save dir>/memory/<levelKey>.json`.

**Segments** are keyed by the hash of the *entry state* — not by frame, not by
position alone. `AnchorState::hash()` quantises x and y to 1/32 of a unit, y
velocity to 1/1024, gravity mod to 1/64, and mixes in gamemode, speed, mini,
upside-down, sideways, dual and grounded.

The quantisation is the whole design: too coarse and unrelated states share an
entry; too fine and the memory never hits, because a live run and a replay never
produce bit-identical floats. Not including the frame is deliberate — the same
physical situation reached at a different time is still the same situation as far
as the inputs that solve it are concerned, which is what lets a segment survive a
rollback.

A remembered segment is only trusted on the **first** attempt at an anchor. If it
kills us, it is deleted outright rather than penalised: a segment that does not
reproduce is worse than no segment, because it wastes an attempt every time.

**Level keys** are `online-<id>`, `official-<id>`, or `local-<fnv64 of the level
string>`. Editing a local level therefore produces a new key, which is correct —
the old macro no longer applies to it.

---

## 7. File map

```
src/core/        Types, Macro, MacroMemory, LevelKey, RngScan
src/practice/    CheckpointLedger, PracticeDirector      <- the new engine
src/bot/         Arbiter                                  <- who solves what
src/learn/       Neat, Sequence                           (vendored, NEATGD)
src/solver/      pathfinder*, real_geometry, wave_mpc     (vendored, Pathfinder)
src/game/        FrameClock, BotRuntime, OfflineSolver, Hooks
src/ui/          HUD, BotUI
gd-sim/          physics simulator                        (vendored, Pathfinder)
```

`src/solver/pathfinder.cpp` is `#include`d by `pathfinder_universal.cpp` and must
never be added to the source list separately — the link fails on duplicate
symbols. This is why `CMakeLists.txt` lists sources explicitly instead of
globbing.

---

## 8. Known gaps

- `reseedFrom` measures divergence, it does not correct it (§5.1).
- Dual-player levels: the director plans player 1 only. The plumbing carries a
  `player2` flag everywhere and `handleButton` is called correctly for both, but
  no solver currently emits player 2 inputs.
- Platformer levels are out of scope — `getEndPosition()` and the whole
  left-to-right progress model assume a classic level.
- The NEAT network path from NEATGD is vendored but the Arbiter currently routes
  the learner through the Climber tape search only. Wiring the evolved network as
  a third planner is the obvious next step.
- Whether `m_randomSeed` survives a checkpoint restore is **unverified** (§4.1).
- All Windows addresses quoted here come from the bindings snapshot dated
  2026-08-08. The SDK builds against `bindings/main` live (`GIT_TAG main`,
  `NO_CACHE YES` in `geode/CMakeLists.txt`), so what a build actually compiles
  against is whatever upstream is at that moment.
