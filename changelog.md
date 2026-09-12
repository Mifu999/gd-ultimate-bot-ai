# Changelog

## v1.0.0

First release. Merges NEATGD and Pathfinder into one mod and adds the
practice-mode director.

**New**

- **Practice director** — solves a level incrementally, anchored on GD's own
  checkpoints. Commits a stretch once it survives past the anchor, plants the
  next checkpoint, and rolls checkpoints back one at a time when it gets stuck.
- **Macro memory** — per-level store of solved stretches, keyed by the physical
  state they start from. A stretch learned once is replayed instead of
  re-searched, across sessions.
- **Arbiter** — routes each stretch to memory, then the simulator, then the
  learner, and feeds each engine's results to the other.
- **Normal-mode validation** — a finished macro is replayed from frame 0 with
  practice mode off before it is marked CERTIFIED.
- **RNG detection** — counts the four seed-consuming triggers and reports that
  the macro may not reproduce on such levels.
- **Offline mode** — the classic whole-level Pathfinder solve, kept for short
  levels the simulator can just answer outright.
- Export to `.gdr2` and to JSON.

**Fixed relative to the sources this builds on**

- `handleButton`'s third parameter is `isPlayer1`, not `player2`. The Pathfinder
  fork passes `player2` directly, which inverts the two players on dual levels.
- Frames come from an integer counter restored from the checkpoint ledger, not
  from `attemptTime * 240`, per the GDReplayFormat spec.
- Built at C++23, which Geode 5.10.1 requires. The fork still specifies C++20.
