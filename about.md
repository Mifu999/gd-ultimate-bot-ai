# GD Ultimate Bot AI

A bot and a learning AI that solve a level **together**, in practice mode,
checkpoint by checkpoint — and write the result out as a macro.

## How it works

Turn it on from the pause menu and pick **Practice run**. The mod takes the level
into practice mode and starts working forwards:

- it plants a checkpoint every time it clears a stretch;
- a stretch that is already cleared is **never replayed** — the run continues
  from the last checkpoint;
- when a spot blocks it, it removes checkpoints **one at a time** and re-searches
  from further back with a longer horizon, until the player is unstuck;
- everything it learns is written to a per-level memory, so the next run walks
  straight through what it already knows.

Two engines share the work. **Pathfinder** simulates GD's physics offline and
finds frame-exact lines where it can. The **learner** takes over where the
simulator is wrong or blind, and hands its results back so the simulator stays
in step. Both read the same death histogram, so hard spots get a wider search
before the first failure rather than after the twentieth.

## The macro

Exports as `.gdr2` (openable in xdBot, Eclipse, MegaHack) or as a readable JSON.

A macro is only marked **CERTIFIED** once it has been replayed end to end from
frame 0 with practice mode **off**. Practice-mode checkpoint restore is thorough
but not provably exact, and "it worked in practice" is a weaker claim than "this
macro plays the level". The mod does not conflate the two.

## Levels with RNG

Geometry Dash keeps one random seed per level, redrawn on **every attempt**
(though it survives a checkpoint respawn). On a level that uses Random, AdvRand,
Advanced Follow or Edit AdvFollow triggers, a finished macro is therefore not
guaranteed to reproduce. The mod counts those triggers when the run starts and
tells you, rather than promising a determinism the game does not offer.

## Credits

- **Pathfinder** and `gd-sim` — camila314, and the fork by candyzp
- **NEATGD** — Mifu
- **GDReplayFormat** — maxnut
- Practice-mode and RNG behaviour verified against the official Geode bindings,
  the Library of Geometria and the gd-info-explorer object table.
