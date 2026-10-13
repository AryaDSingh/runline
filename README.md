# Runline

First-person parkour movement in Unreal Engine 5.8: a C++ movement core, verified bit-exact against its
reference implementation, with a playable rooftop course and time trial.

*Runline is a working codename. This is a personal portfolio project in progress.*

## What's here

- **RunlineCore**: a plain C++20 library (no Unreal) that runs the whole movement system: sprinting,
  jumping and landing, vaults, wallruns, wallclimbs, ledge grabs and shimmies, slides, rolls, 180 turns.
  It is a function-by-function port of [faith-runner](https://github.com/tnrjns/faith-runner)'s Rust
  movement crate, made with its author's permission.
- **Parity, proven by tests**: 176 recorded traces from the Rust original replay frame by frame through the
  C++ core with **bit-identical** results (every position, velocity, state and event), and the 157 box-world
  traces replay bit-identically through Unreal's collision too. The test suites run headless (doctest for the core, Unreal automation tests for
  the engine side).
- **The Unreal 5.8 project**:
  - a movement component that steps the core at a fixed 60 Hz with interpolated rendering;
  - Unreal collision behind the core's world interface;
  - Enhanced Input (keyboard and gamepad), camera effects, an options and pause menu;
  - a baked rooftop course with checkpoints and a time trial;
  - a motion-capture body animated by the movement state.

## Build and run

Requirements: Windows, Unreal Engine 5.8 (default `D:\UE_5.8`, or set `UE_ROOT`), Visual Studio 2022, CMake,
Git LFS.

```powershell
git lfs install; git lfs pull
powershell -ExecutionPolicy Bypass -File scripts/test.ps1          # core tests (no Unreal needed)
powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1   # builds the editor target, runs the Unreal tests
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\Unreal\Runline\Runline.uproject" -game -windowed -ResX=1600 -ResY=900
```

The body's motion-capture clips come from Epic's free Game Animation Sample and are not in this
repository: `scripts/import-gasp.ps1` imports them from your own copy of the sample. Without them the
project uses the Unreal template's animation.

Controls: WASD to move, Space to jump, Shift/C/Ctrl to crouch, slide and roll, Q to turn 180, R to respawn,
1-9 for checkpoints, Esc for the menu.

## Layout

```
RunlineCore/          the movement core (C++20, CMake) and its tests and golden data
Unreal/Runline/       the Unreal 5.8 project (Source/Runline: adapter, runner, menu, courses, body animation)
tools/                golden-data and trace tools (run against the Rust reference)
scripts/              build, test, bake and import scripts
docs/                 design specs and implementation plans
```

## Credits

The movement system is ported from **faith-runner** by tnrjns, used and adapted with the author's
permission. faith-runner's rules and values are in turn derived from *Mirror's Edge* (DICE / EA), which is
the design reference; the source comments keep that provenance. No game files are included, and this
project is not affiliated with or endorsed by EA or DICE. Full credits, including the Unreal template and
asset licenses: [CREDITS.md](CREDITS.md).
