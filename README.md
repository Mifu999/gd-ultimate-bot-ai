# GD Ultimate Bot AI

Geode mod for Geometry Dash **2.2081**, built against **Geode 5.10.1**.

Merges two existing mods — NEATGD (a learning AI) and Pathfinder (an offline
physics solver) — and adds a practice-mode director that solves a level
incrementally using GD's own checkpoint system.

Read **[ARCHITECTURE.md](ARCHITECTURE.md)** first if you are going to work on
this. It records which behaviours were verified against a source, which were
not, and the three traps that shaped the code.

## Building

```bash
export GEODE_SDK=/path/to/geode
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

Requires the Geode CLI for packaging. `gd-sim` and the solver are vendored;
`libGDR` is fetched by CPM.

### One build trap

`src/solver/pathfinder.cpp` is `#include`d by `pathfinder_universal.cpp`. It must
**not** appear in the source list on its own or the link fails on duplicate
symbols. This is why `CMakeLists.txt` lists sources explicitly instead of using
`GLOB`.

## Layout

| Path | What |
|---|---|
| `src/core/` | macro model, per-level memory, level keys, RNG scan |
| `src/practice/` | the checkpoint ledger and the director |
| `src/bot/` | the arbiter — decides who solves each stretch |
| `src/learn/` | NEAT and the climber tape search (from NEATGD) |
| `src/solver/` | the Pathfinder solver (from candyzp's fork) |
| `src/game/` | frame clock, runtime, hooks, offline solver |
| `src/ui/` | HUD and popup |
| `gd-sim/` | offline physics simulator (from Pathfinder) |

## Status

Works end to end on classic levels. Known gaps are listed in
[ARCHITECTURE.md §8](ARCHITECTURE.md) — the short version is: player 1 only, no
platformer support, and gd-sim divergence is measured but not corrected.
