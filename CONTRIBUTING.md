# Kairos — working guide

A Geode mod for Geometry Dash 2.2081 (Windows) built on [gdsolver](https://github.com/gdsolver/gdsolver)
(MIT): its reachability DP finds a level's inputs and the game verifies them; Kairos adds human
inputs, solution optimization, frame windows, rhythm practice and a guard against other mods.

## Layout

```
src/mod/     gdsolver's mod: the solve / replay loop (repair.hpp), hooks, sessions
src/solver/  gdsolver: level export, moving-geometry recording, psnap, section solver
dp/          gdsolver's solver core (dp/src/dp/) and the leveldp CLI
src/kairos/  Kairos in the game: exact engine (GameEngine), jobs, K menu, stats popup,
             rhythm overlay, ModGuard, SolverState (the only file reading src/mod state)
src/kpsim/   Kairos, Geode-free: search (Timeline, FrameWindows, Optimizer, Pathfinder),
             macro I/O (.gdr2, .gdr, solver plans), engine transcription
py/ mcp/ data/ docs/ tools/  gdsolver's tooling, solutions and documentation
```

Local only, never versioned: `src/sim`, `src/gdp-2.2`, `src/dev`, `test/`, `testlevel/`, and the
local scripts listed in `.gitignore`.

## Build

`./deploy.sh` builds and installs into the local Geode mods folder. It uses the Geode bindings
pinned at f050fd74 (`../Kairos-bindings`, `GEODE_BINDINGS_REPO_PATH`): newer bindings make
`GJGameState` non-copyable, which `src/solver/psnap.hpp` needs. After changing the bindings, run
`python tools/gen_podfields.py` to regenerate `src/kairos/PodFields.inc`.

## Rules

1. **Solving is always cold.** No seeds, no previous solutions, no external ground truth. The
   only exception is an explicit resume of the same lineage, and a result reached that way is
   never reported as solved cold. A run's fixups live only in that run. Kairos's Optimize starts
   from a solution, so it is not a solve: it only reports what the game replayed from the start.
2. **Keep gdsolver's code close to upstream.** Change `src/mod`, `src/solver` and `dp/` only for a
   real fix, mark it with a `// Kairos:` comment, and keep Kairos code in `src/kairos` and
   `src/kpsim`, so upstream changes can still be brought over.
3. **Ask the game, do not guess.** Behaviour, hitboxes and constants come from the disassembly or
   a measurement, never from a single coincidence. A plan is only "solved" once the game replayed
   it from the start.
4. **gdsolver changes are proven.** A change reaching the DP or the loop is checked with
   `python py/quick_regress.py` (byte-identical output) and, if it reaches the loop,
   `python py/cold_regress.py --one-session` against `data/cold_baseline.json` (see
   `docs/RESULTS.md`). One session, because a launch per level never exercises the cleanup
   between levels: a leak of exactly that kind (one level's moving-geometry recording adopted by
   the next) sat under a green 22/22 for months.
5. **Kairos code: terse.** Comments of a few words, only where a name is not enough. Public
   tree in English.

## Bringing gdsolver changes over

This repository is a fork of gdsolver without its history. Its gdsolver code is based on gdsolver
`2cddf0e` (2026-10-11). Later upstream changes come over as a patch, then the checks of rule 4:

```
git remote add upstream https://github.com/gdsolver/gdsolver.git   # once
git fetch upstream
git diff 2cddf0e upstream/main -- dp src/mod src/solver py mcp docs tools data | git apply -3
```

Then update the base commit above.

## Safety

While a bot drives (solve, replay, Kairos jobs, Watch) nothing is recorded: achievements,
statistics, coins, the level's record (`botDriving()`, config.hpp). Replays are bot input; nothing
here may be used to claim a record.
