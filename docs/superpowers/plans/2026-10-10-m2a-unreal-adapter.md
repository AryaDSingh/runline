# M2a: Unreal Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Faith's movement running inside Unreal 5.8. RunlineCore is linked into the game module and stepped at a fixed 60 Hz by a movement component. It is played with keyboard, mouse or pad on faith-runner's greybox and Rooftops courses, which are spawned from exported data. Every box trace replays bit-exact through Unreal's world.

**Architecture:** CMake builds RunlineCore as a Release static library, run as a UBT pre-build step. The `Runline` module links it. `FUnrealWorld` implements `runline::World` as an Unreal **broadphase** plus the core's own **narrowphase**:
- Unreal's overlap query finds the nearby `UBoxComponent`s.
- The core's exact box maths (the same code BoxWorld runs) answers the sweep.

So in Unreal, collision answers are bit-identical to BoxWorld, and the committed traces become the adapter's test. `URunlineMovementComponent` (a `UCharacterMovementComponent` that bypasses CMC physics) owns a `runline::Controller`:
- it latches input between steps;
- it runs whole 1/60 s steps;
- it puts the capsule at the stepped feet;
- it interpolates only the camera.

All unit and axis conversion lives in one header, `RunlineUnits.h`.

**Tech Stack:** UE 5.8 (`D:\UE_5.8`), C++20, MSVC (VS 2022), CMake (bundled with VS), Enhanced Input (built in code, no assets), Unreal automation tests run headless, doctest for the core, and Rust (tools/golden) for the level exporter.

**Spec:** `docs/superpowers/specs/2026-10-09-faith-movement-port-design.md` (M2, §3 "Unit boundary" and "Fixed step", §6 Risks)

**M2 is split into three plans**, like M1:
- **M2a (this plan): the adapter.** A playable runner on box courses, with the plain camera from `Controller::GetView`.
- **M2b: feel layers.** Port `look.rs` (look limits, swan neck) and `camera.rs` (CameraFx: bob, sway, tilt, kicks, shake, FOV, speed blur) to the core with goldens, plus the template's first-person arms.
- **M2c: Rooftops as a real map.** An Editor Python script bakes the exported course into `Rooftops.umap` (with checkpoint and finish volumes), plus an automation test that runs a scripted Rooftops route through the real component.

## Deliberate deviation from the spec (flag to the user before executing)

The spec says FUnrealWorld uses "UWorld sweep/overlap queries". This plan uses Unreal's overlap query only as a broadphase, and keeps the narrowphase in the core:
- **Why:** the core's contact rules are exact. A box only touching sideways is passed; one touching ahead is hit at t = 0; solids a box starts inside are ignored; overlaps are strict by 1e-4 m. Chaos box sweeps handle every one of those differently (contact offsets, start-penetration MTD, GJK tolerance). Feel and trace parity would drift in exactly the corner cases the M1 mutants showed nobody tests.
- **What it buys:** with the core's narrowphase, FUnrealWorld equals BoxWorld bit for bit. The 156 committed box traces (after Task 1) then test the whole Unreal adapter.
- **What it costs:** M2a supports only `UBoxComponent` colliders. Any other blocking primitive in range is ignored, with a one-time warning naming it. Rooftops and the greybox are all boxes. Triangle meshes, through the core's MeshWorld narrowphase, come when a level needs them (M6).

## Global Constraints

- RunlineCore stays plain C++20 with no Unreal headers. Metres, Y up, float everywhere. MSVC `/fp:strict`. (CLAUDE.md)
- "Unit/axis conversion to Unreal (cm, Z up) happens only in the Unreal adapter (M2)": only `RunlineUnits.h` converts. Nothing else in the Unreal layer touches raw core coordinates. (spec §3, CLAUDE.md)
- "The core always steps at 1/60 s." "Mouse look is not stepped: raw yaw and pitch are applied every render frame and the accumulated look is fed to the core each step." "Only the camera and arms are interpolated between steps; the capsule stays at the real stepped position." (spec §3)
- Melee and takedown inputs are always false. (spec §3)
- Credit faith-runner; never commit Mirror's Edge files or decompiled scripts. (CLAUDE.md)
- Sources stay ASCII. `source_tests.cpp` enforces this for RunlineCore, and the Unreal sources follow the same rule.
- Float literals carry an `f` suffix in core code. No `std::min`/`std::max` on floats in core code; use `FMin`/`FMax`/`FClamp`. (M1 conventions)
- Unreal engine root: `D:\UE_5.8`. Project: `Unreal/Runline/Runline.uproject`. Module: `Runline`.
- Merging to master is the user's decision.

## Review Focus

1. **Frame rate extremes.** Running the same held inputs at 30, 60 and 144 fps must give the same core state after the same number of steps. A 0.5 s hitch must not spiral (at most 5 steps per frame). Test: Task 6 `Runline.Movement.FrameRates` and `Runline.Movement.Hitch`.
2. **A button tapped between two steps.** At 144 fps, jump pressed and released within one 1/60 s window must reach the core once (pressed, and held on that step). At 30 fps, one press must not repeat on the frame's second step. Test: Task 6 `Runline.Movement.InputLatch`.
3. **Resting on and touching geometry.** Standing on a floor, flush against a wall, or starting inside a box must answer exactly as BoxWorld does. Test: Task 5's golden parity, which includes the M0 touching cases in `sweeps.csv`, plus `Runline.World.TouchingAndInside`.
4. **Turning 180 in the air and landing on your back must not crash the game.** `LayOnGround` is ported in Task 1. Any other NotPorted state is caught by the component, logged once, and turned into a respawn. Test: Task 1 replay, plus Task 6 `Runline.Movement.NotPortedGuard`.
5. **The pawn's own capsule, and non-blocking components, are invisible to the core.** FUnrealWorld ignores its owner and anything that doesn't block. Test: Task 5 `Runline.World.IgnoresOwnerAndTriggers`.

## File Structure

```
RunlineCore/
  CMakeLists.txt                       modify: RUNLINE_TESTS option (the UE build skips doctest)
  include/runline/BoxWorld.h           modify: expose SweepBoxes / OverlapsBoxes (the narrowphase)
  include/runline/Controller.h         modify: LayOnGroundState decl
  src/BoxWorld.cpp                     modify: narrowphase out of the anonymous namespace
  src/Controller.cpp                   modify: lay_on_ground port, dispatch
  src/ControllerStubs.cpp              modify: LayOnGround removed from the unported list
  tests/TraceFormat.h                  create: trace structs + Parse (no doctest), moved from Trace.h
  tests/Trace.h                        modify: keeps Load/All/Boxes/Mesh (doctest), includes TraceFormat.h
  tests/Replay.h                       create: Apply/Diff/DiffExt/ReplayAccess/Replay (no doctest), moved from replay_tests.cpp
  tests/replay_tests.cpp               modify: just the TEST_CASEs
scripts/
  build-core-ue.ps1                    create: Release static lib for Unreal (UBT pre-build step)
  test-unreal.ps1                      create: build the editor target + run Runline.* automation tests headless
tools/golden/src/levels.rs             create: exports greybox and rooftops to Unreal/Runline/Data/Levels/*.csv
tools/golden/src/main.rs               modify: call levels::write_all
tools/trace/selected.txt               modify: on-your-back traces
Unreal/Runline/
  Data/Levels/greybox.csv, rooftops.csv  generated, committed
  Source/Runline.Target.cs, RunlineEditor.Target.cs   modify: PreBuildSteps
  Source/Runline/Runline.Build.cs      modify: link core, exceptions, include paths
  Source/Runline/Runner/
    RunlineCoreBridge.h                core headers behind THIRD_PARTY_INCLUDES_START
    RunlineUnits.h                     THE conversion: positions, directions, extents, boxes, yaw/pitch
    UnrealWorld.h/.cpp                 FUnrealWorld: broadphase (UE overlap) + core narrowphase
    RunlineMovementComponent.h/.cpp    fixed step, input latch, capsule, interpolated view, NotPorted guard
    RunlineCourse.h/.cpp               ARunlineCourse: spawns boxes, lights, checkpoints from a level CSV
    RunlineRunner.h/.cpp               ARunlineRunner: ACharacter + component + camera + Enhanced Input in code
    RunlineRunGameMode.h/.cpp          default pawn = runner; spawns the course named by ?course=
  Source/Runline/Tests/
    CoreLinkTests.cpp                  Runline.Core.*
    UnitsTests.cpp                     Runline.Units.*
    UnrealWorldTests.cpp               Runline.World.*  (golden parity + every box trace)
    MovementComponentTests.cpp         Runline.Movement.*
    RunlineTestWorld.h                 helper: transient game world + box spawning for tests
```

### The axis mapping (the spec asks for it in writing)

Core frame: metres, **x right, y up, z back**, a right-handed frame. At yaw 0 the core faces −Z: `ForwardOf(yaw) = (−sin yaw, 0, −cos yaw)`, `RightOf(yaw) = (cos yaw, 0, −sin yaw)`. Positive yaw turns **left**, and positive pitch looks **up**. `View::roll` is positive when the head tilts right.

Unreal frame: centimetres, **X forward, Y right, Z up**, a left-handed frame. Positive yaw turns **right**, and positive pitch looks up.

| Core | Unreal |
|---|---|
| position `(x, y, z)` m | `FVector(−z, x, y) × 100` cm |
| direction / velocity `(x, y, z)` | `FVector(−z, x, y)` (× 100 for velocities) |
| half extent `(hx, hy, hz)` | `FVector(hz, hx, hy) × 100` (no sign: extents are sizes) |
| Aabb `{min, max}` | `FBox(FVector(−max.z, min.x, min.y), FVector(−min.z, max.x, max.y)) × 100` |
| yaw `ψ` rad | `FRotator.Yaw = −ψ · 180/π` |
| pitch `θ` rad | `FRotator.Pitch = θ · 180/π` |
| roll `ρ` rad (positive: head right) | `FRotator.Roll = ρ · 180/π` (M2b checks this sign against the camera's right vector) |

The matrix `(x,y,z) → (−z,x,y)` has determinant −1. It is a reflection, which is what changing handedness requires. Right maps to right, up to up, and back to back. So core forward at yaw 0, `(0,0,−1)`, maps to Unreal `(1,0,0)` = forward, and `ForwardOf(ψ)` maps to `(cos ψ, −sin ψ, 0)`, which is Unreal yaw −ψ. Positions round-trip **exactly**, because `float × 100` and `/ 100` in double recover the float: 24 + 7 bits fit in a 53-bit mantissa.

### The input mapping (from faith-runner's `src/main.rs`)

| Action | Keyboard / mouse | Pad | Core `Input` |
|---|---|---|---|
| Move | W/S forward/back, D/A right/left | left stick | `moveAxis` = sum clamped to length 1; `strafeRaw` = x clamped to ±1 |
| Look | mouse delta × 0.0022 rad, x negated | right stick × 3.2 rad/s × dt, x negated | `look` (x: positive turns left; y: positive looks up) |
| Jump | Space | A (south), left bumper | `jumpPressed` edge, `jumpHeld` level |
| Crouch | Left Shift, C, Left Ctrl | B (east), left trigger | `crouchPressed` edge, `crouchHeld` level |
| 180 turn | Q | Y (north) | `turnPressed` edge |
| Respawn | R | Back/Select | `Controller::Respawn()` |
| Melee / takedown | none | none | always false |

---

### Task 1: Port LayOnGround (on your back, getting up, back roll)

The air 180 (`turnedInAir`) lands you in `LayOnGround`, which still throws NotPorted. In Unreal that is a crash on Q-then-land, which is the first thing a playtester tries. Port `lay_on_ground` from controller.rs (lines 1861-1896 at 9470d755) and replay faith-runner's two tests that cover it.

**Files:**
- Modify: `RunlineCore/include/runline/Controller.h` (declare `LayOnGroundState`, beside `RollState`)
- Modify: `RunlineCore/src/Controller.cpp` (dispatch in substep; the function after `RollState`)
- Modify: `RunlineCore/src/ControllerStubs.cpp` (the comment on `UnportedState`: only SoftLand left)
- Modify: `RunlineCore/tests/replay_tests.cpp` (`StateFromDebug` learns `LayOnGround { t: T, getting_up: None|Some(G), back_roll: B }`)
- Modify: `tools/trace/selected.txt`

**Interfaces:**
- Consumes: `LayOnGround{float t; std::optional<float> gettingUp; bool backRoll;}` (already in the State variant), `MoveBy(Vec3, const World&)`, `TryStand(const World&)`, `Integrate(float, const World&, bool)`, `tuning.layOnGroundFallTime/GetUpTime/backRollTime/backRollDistance`
- Produces: `void Controller::LayOnGroundState(float dt, LayOnGround s, const Input& input, const World& world);`

- [ ] **Step 1: Select the traces (RED).** Append to `tools/trace/selected.txt`:

```
# M2a: on your back (TdMove_LayOnGround)
tests__air_turn_lands_you_on_your_back__1
tests__back_roll_off_your_back__1
```

Run: `bash tools/trace/make-traces.sh`, then `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`.
Expected: FAIL. `air_turn_lands_you_on_your_back__1` fails with `reached not ported: state On your back`. `back_roll_off_your_back__1` fails with `trace sets state ..., which replay can't rebuild yet` (its test starts the controller in LayOnGround).

- [ ] **Step 2: Teach the replay to rebuild LayOnGround.** In `StateFromDebug` (replay_tests.cpp), before the LedgeHang branch:

```cpp
    if (d.rfind("LayOnGround {", 0) == 0) {
        float t = 0.0f, g = 0.0f;
        char backRoll[8] = {};
        if (std::sscanf(d.c_str(), "LayOnGround { t: %f, getting_up: Some(%f), back_roll: %7[a-z] }", &t, &g, backRoll) == 3)
            return LayOnGround{t, g, std::string(backRoll) == "true"};
        if (std::sscanf(d.c_str(), "LayOnGround { t: %f, getting_up: None, back_roll: %7[a-z] }", &t, backRoll) == 2)
            return LayOnGround{t, std::nullopt, std::string(backRoll) == "true"};
        throw std::runtime_error("can't read state " + d);
    }
```

Update the doc comment above `StateFromDebug` to list both forms. Run the tests. Expected: both traces now fail with `reached not ported: state ...`.

- [ ] **Step 3: Port it.** Declare in Controller.h, next to `RollState`:

```cpp
    void LayOnGroundState(float dt, LayOnGround s, const Input& input, const World& world);
```

Dispatch in the substep's state switch where `LayOnGround` currently falls to `UnportedState`:

```cpp
        else if (auto* s = std::get_if<LayOnGround>(&state)) LayOnGroundState(dt, *s, input, world);
```

The function (Controller.cpp, after `RollState`), a line-by-line port:

```cpp
/// TdMove_LayOnGround: lie there until you ask to get up: jump, or forward once the fall onto
/// your back has played (LayOnGroundFallTime). Pull back instead and you roll backwards
/// (GetUpBack). Getting up takes LayOnGroundGetUpTime (BackRollTime for the roll).
void Controller::LayOnGroundState(float dt, LayOnGround s, const Input& input, const World& world) {
    const Tuning tu = tuning;
    const float t = s.t + dt;
    vel = Vec3{0.0f, FMin(vel.y, 0.0f), 0.0f};
    const bool hintDown = std::fabs(input.moveAxis.x) <= 0.3f && input.moveAxis.y < -0.8f;
    std::optional<float> gettingUp;
    bool backRoll = false;
    if (s.gettingUp) {
        gettingUp = *s.gettingUp + dt;
        backRoll = s.backRoll;
    } else if (hintDown) {
        events.push_back({EventKind::BackRoll});
        gettingUp = 0.0f;
        backRoll = true;
    } else if (jumpBuffer_ > 0.0f || (input.moveAxis.y > 0.8f && t >= tu.layOnGroundFallTime)) {
        jumpBuffer_ = 0.0f;
        gettingUp = 0.0f;
    }
    const float done = backRoll ? tu.backRollTime : tu.layOnGroundGetUpTime;
    if (gettingUp && *gettingUp >= done) {
        if (backRoll) {
            // EvadeRoll's root motion: you end up behind where you lay.
            const Vec3 back = -ForwardOf(yaw) * tu.backRollDistance;
            MoveBy(back, world);
            crouched = !TryStand(world);
        }
        state = Ground{};
    } else {
        state = LayOnGround{t, gettingUp, backRoll};
    }
    Integrate(dt, world, true);
}
```

Before writing it, read the Rust (controller.rs 1861-1896) side by side. Check the `match` arm order (an existing `getting_up` wins over `hint_down`, which wins over jump/forward) and that `self.tuning.clone()` maps to a copy. Check `TryStand`'s exact C++ name in Controller.h.

- [ ] **Step 4: Run the tests (GREEN).** Run `scripts/test.ps1`. Expected: 70/70 pass, and both traces replay bit-exact, including the `hid` record.

- [ ] **Step 5: Mutation check (read the result before committing).** Change `t >= tu.layOnGroundFallTime` to `t > tu.layOnGroundFallTime + 0.1f`, then run. Expected: FAIL in `air_turn_lands_you_on_your_back__1` (the test pushes forward to get up). Revert. Change `backRollDistance` to `* 0.9f`, then run. Expected: FAIL in `back_roll_off_your_back__1`. Revert. If either survives, add a Runline scenario in the trace patch, as in M1d, before committing.

- [ ] **Step 6: Commit.**

```bash
git add RunlineCore tools/trace/selected.txt RunlineCore/tests/golden/traces
git commit -m "M2a: port LayOnGround (on your back, get up, back roll), replay-checked"
```

---

### Task 2: Make the core's narrowphase and the replay reusable outside doctest

FUnrealWorld (Task 5) needs BoxWorld's exact sweep and overlap maths on a candidate list. The Unreal tests need to replay traces without doctest. This task is a pure refactor: no behaviour change, and all tests stay green.

**Files:**
- Modify: `RunlineCore/include/runline/BoxWorld.h`, `RunlineCore/src/BoxWorld.cpp`
- Create: `RunlineCore/tests/TraceFormat.h` (moved out of `Trace.h`: the structs, `Split`, `Need`, `ParseSnapshot`, `Parse`; includes no doctest)
- Modify: `RunlineCore/tests/Trace.h` (includes `TraceFormat.h`; keeps `Dir`, `Load`, `All`, `Boxes`, `Mesh`)
- Create: `RunlineCore/tests/Replay.h` (moved out of `replay_tests.cpp`: `Same`, `Num`, `StateFromName`, `StateFromDebug`, `Apply`, `Diff`, `DiffExt`, `ReplayAccess`, `Replay`; header-only, `inline`, namespace `replay`)
- Modify: `RunlineCore/tests/replay_tests.cpp`
- Modify: `RunlineCore/CMakeLists.txt`

**Interfaces:**
- Produces (BoxWorld.h):

```cpp
/// BoxWorld's sweep over an explicit list of boxes, in list order (ties keep the earlier box):
/// the narrowphase FUnrealWorld runs on Unreal's broadphase candidates.
std::optional<SweepHit> SweepBoxes(const std::vector<Aabb>& boxes, Vec3 half, Vec3 start, Vec3 delta);
/// Whether any of `boxes` overlaps `region` strictly (touching faces don't count).
bool OverlapsBoxes(const std::vector<Aabb>& boxes, const Aabb& region);
```

- Produces (Replay.h):

```cpp
namespace replay {
/// Replays a trace; "" if every frame matches, else where and how it first diverged.
/// `worldFor(i)` returns the World for trace world i (BoxWorld/MeshWorld on the desktop,
/// FUnrealWorld in Unreal); it is called once per frame, so build worlds up front.
std::string Replay(const trace::Trace& t, const std::function<const runline::World&(std::size_t)>& worldFor);
}
```

- [ ] **Step 1: Move the narrowphase.** In BoxWorld.cpp, `SweepAabb`, `ContactOnBox` and `SlabHit` stay in the anonymous namespace; move `SweepBoxes` (body unchanged) out of it into `namespace runline`, and add:

```cpp
bool OverlapsBoxes(const std::vector<Aabb>& boxes, const Aabb& region) {
    for (const Aabb& b : boxes)
        if (b.Overlaps(region)) return true;
    return false;
}
```

`BoxWorld::Sweep` keeps its candidate filter and calls `SweepBoxes(candidates, ...)`. `BoxWorld::Overlaps` returns `OverlapsBoxes(boxes, region)`. Declare both in BoxWorld.h with the doc comments above.

- [ ] **Step 2: Split Trace.h.** Create `TraceFormat.h` with the exact contents of Trace.h's `namespace trace` from `struct Snapshot` through `Parse`. Its includes are `<algorithm> <cmath> <istream> <optional> <stdexcept> <string> <vector> "runline/BoxWorld.h" "runline/MeshWorld.h"`, and it has no doctest. `Trace.h` then is:

```cpp
#pragma once
// Reads the per-frame traces tools/trace/make-traces.sh records from faith-runner's own tests.
// The format and parser are in TraceFormat.h (no doctest, so Unreal's tests can use them too).
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "TraceFormat.h"

namespace trace {
// Dir, Load, All, Boxes, Mesh: unchanged, moved here verbatim.
}
```

- [ ] **Step 3: Move the replay into Replay.h.** Move every function above the first `TEST_CASE` in replay_tests.cpp into `Replay.h`, under `namespace replay` (and `runline::ReplayAccess` stays in `namespace runline`). Mark them `inline`. Change `Replay` to take `worldFor`. Inside the frame loop, replace the `wd.tris.empty() ? Boxes : Mesh` construction with:

```cpp
        if (!t.worlds[f.world].fixtures.empty()) return "frame " + std::to_string(i) + ": fixtures not ported yet";
        try {
            c.Step(f.in.dt, in, worldFor(f.world));
        } catch (const NotPorted& e) {
            return "frame " + std::to_string(i) + ": reached " + e.what();
        }
```

In replay_tests.cpp, build the worlds once per trace:

```cpp
std::string ReplayDesktop(const trace::Trace& t) {
    std::vector<std::unique_ptr<World>> worlds;
    for (const auto& wd : t.worlds) {
        if (wd.tris.empty()) worlds.push_back(std::make_unique<BoxWorld>(trace::Boxes(wd)));
        else worlds.push_back(std::make_unique<MeshWorld>(trace::Mesh(wd)));
    }
    return replay::Replay(t, [&](std::size_t i) -> const World& { return *worlds[i]; });
}
```

The TEST_CASEs call `ReplayDesktop`.

- [ ] **Step 4: Add `RUNLINE_TESTS` to CMake.** Wrap everything from `include(FetchContent)` to the end in `if(RUNLINE_TESTS)` / `endif()`, after `option(RUNLINE_TESTS "Build RunlineCore's doctest suite" ON)`. This lets the Unreal build configure without fetching doctest.

- [ ] **Step 5: Run the tests.** Run `scripts/test.ps1`. Expected: 70/70 pass, with the same assertion count as before Task 2 plus Task 1's traces. Mutation sanity: make `OverlapsBoxes` return `false` and run. Expected: FAIL across golden and replay tests. Revert.

- [ ] **Step 6: Commit.**

```bash
git add RunlineCore
git commit -m "M2a: expose BoxWorld's narrowphase; trace parser and replay usable without doctest"
```

---

### Task 3: Link RunlineCore into the Unreal module, and run Unreal tests headless

**Files:**
- Create: `scripts/build-core-ue.ps1`, `scripts/test-unreal.ps1`
- Modify: `Unreal/Runline/Source/Runline.Target.cs`, `Unreal/Runline/Source/RunlineEditor.Target.cs`, `Unreal/Runline/Source/Runline/Runline.Build.cs`
- Create: `Unreal/Runline/Source/Runline/Runner/RunlineCoreBridge.h`
- Create: `Unreal/Runline/Source/Runline/Tests/CoreLinkTests.cpp`
- Modify: `.gitignore` (add `build/`, if `build/RunlineCore` isn't already covered)
- Modify: `CLAUDE.md` (Build and test: the two new scripts)

**Interfaces:**
- Produces: `#include "RunlineCoreBridge.h"`, which gives Unreal code `runline::Controller`, `runline::BoxWorld`, `runline::World`, `runline::Tuning`, `runline::NotPorted`. Also `scripts/test-unreal.ps1 [-Filter Runline.]`, which exits non-zero on any failure.

- [ ] **Step 1: Write the failing link test.** Create `Tests/CoreLinkTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "RunlineCoreBridge.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCoreLinkTest, "Runline.Core.FallsAndLands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRunlineCoreLinkTest::RunTest(const FString& Parameters)
{
    // The core, as compiled by CMake for Unreal, runs a plain drop: 2 m onto a floor.
    runline::BoxWorld World;
    World.Add(runline::Aabb::New({-50.0f, -1.0f, -50.0f}, {50.0f, 0.0f, 50.0f}));
    runline::Controller C(runline::Tuning{}, {0.0f, 2.0f, 0.0f}, 0.0f);
    for (int I = 0; I < 120; ++I) C.Step(1.0f / 60.0f, runline::Input{}, World);
    TestTrue(TEXT("on the ground"), std::holds_alternative<runline::Ground>(C.state));
    TestEqual(TEXT("feet on the floor"), C.feet.y, 0.0f);
    return true;
}

#endif
```

- [ ] **Step 2: The build script.** Create `scripts/build-core-ue.ps1`:

```powershell
# Builds RunlineCore as a Release static library for the Unreal module (/MD, /fp:strict).
# Run by UBT as a pre-build step (Runline*.Target.cs); safe to run by hand.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$build = Join-Path $root 'build\RunlineCore-ue'
& $cmake -S (Join-Path $root 'RunlineCore') -B $build -DRUNLINE_TESTS=OFF | Out-Null
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build $build --config Release --target RunlineCore
exit $LASTEXITCODE
```

- [ ] **Step 3: Link it.** In `Runline.Build.cs`, add `using System.IO;` and, at the end of the constructor:

```csharp
		// RunlineCore: plain C++20, built by scripts/build-core-ue.ps1 (a pre-build step of both
		// targets) into a Release static library. Its NotPorted guard needs exceptions.
		string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", ".."));
		PublicIncludePaths.Add(Path.Combine(RepoRoot, "RunlineCore", "include"));
		PublicAdditionalLibraries.Add(Path.Combine(RepoRoot, "build", "RunlineCore-ue", "Release", "RunlineCore.lib"));
		bEnableExceptions = true;
		PublicIncludePaths.Add("Runline/Runner");
		// The adapter tests replay the core's traces and goldens.
		PrivateIncludePaths.Add(Path.Combine(RepoRoot, "RunlineCore", "tests"));
		PublicDefinitions.Add("RUNLINE_GOLDEN_DIR=\"" + Path.Combine(RepoRoot, "RunlineCore", "tests", "golden").Replace('\\', '/') + "\"");
```

In both Target.cs files, in the constructor:

```csharp
		PreBuildSteps.Add("powershell -NoProfile -ExecutionPolicy Bypass -File \"$(ProjectDir)\\..\\..\\scripts\\build-core-ue.ps1\"");
```

Create `Runner/RunlineCoreBridge.h`:

```cpp
#pragma once
// RunlineCore's headers for Unreal code. The core is plain C++20 (std::optional, std::variant,
// exceptions for NotPorted) with its own warning settings, so it is included as third-party code.
// Nothing outside RunlineUnits.h may convert its coordinates.
THIRD_PARTY_INCLUDES_START
#include "runline/BoxWorld.h"
#include "runline/Controller.h"
THIRD_PARTY_INCLUDES_END
```

- [ ] **Step 4: The headless test runner.** Create `scripts/test-unreal.ps1`:

```powershell
# Builds the Runline editor target and runs Runline.* automation tests headless.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 [-Filter Runline.World]
param([string]$Filter = 'Runline.')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$ue = if ($env:UE_ROOT) { $env:UE_ROOT } else { 'D:\UE_5.8' }
$project = Join-Path $root 'Unreal\Runline\Runline.uproject'
& "$ue\Engine\Build\BatchFiles\Build.bat" RunlineEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReload
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$log = Join-Path $root 'build\unreal-tests.log'
& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project "-ExecCmds=Automation RunTests $Filter;Quit" `
    -unattended -nopause -nosplash -nullrhi -NoSound "-abslog=$log" '-testexit=Automation Test Queue Empty'
$text = Get-Content $log -Raw
$passed = ([regex]::Matches($text, 'Test Completed\. Result=\{Success\}')).Count
$failed = ([regex]::Matches($text, 'Test Completed\. Result=\{(Fail|Failed)\}')).Count
Select-String -Path $log -Pattern 'Result=\{Fail|Error: ' | ForEach-Object { $_.Line }
Write-Host "Unreal tests: $passed passed, $failed failed"
if ($failed -gt 0 -or $passed -eq 0) { exit 1 }
exit 0
```

The exact result-line format can vary between engine versions. On the first run, open `build/unreal-tests.log`, confirm the `Test Completed. Result={...}` wording, and adjust both regexes so a known pass and a known failure are counted. Leave the test in Step 1 failing once on purpose (for example `TestEqual(..., 1.0f)`) to see the failure counted, then fix it.

- [ ] **Step 5: Build and run.** Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Core`. Expected: `Unreal tests: 1 passed, 0 failed`. If linking fails with `_ITERATOR_DEBUG_LEVEL` or `RuntimeLibrary` mismatches, the lib was built in Debug: check that `build-core-ue.ps1` builds `--config Release`. If UBT can't find the lib on a clean checkout, the pre-build step didn't run: run the script by hand once and check the Target.cs path.

- [ ] **Step 6: Document and commit.** Add both scripts to CLAUDE.md's "Build and test" list, with one line each.

```bash
git add scripts Unreal/Runline/Source CLAUDE.md .gitignore
git commit -m "M2a: link RunlineCore into the Unreal module; headless Unreal test runner"
```

---

### Task 4: RunlineUnits.h, the one place coordinates convert

**Files:**
- Create: `Unreal/Runline/Source/Runline/Runner/RunlineUnits.h`
- Test: `Unreal/Runline/Source/Runline/Tests/UnitsTests.cpp`

**Interfaces:**
- Produces (namespace `RunlineUnits`, all `inline`):

```cpp
FVector ToUnrealPosition(runline::Vec3 P);          // m, Y up -> cm, Z up
runline::Vec3 ToCorePosition(const FVector& P);
FVector ToUnrealDirection(runline::Vec3 D);          // unit-free (normals, unit vectors)
runline::Vec3 ToCoreDirection(const FVector& D);
FVector ToUnrealVelocity(runline::Vec3 V);           // m/s -> cm/s
FVector ToUnrealExtent(runline::Vec3 Half);          // sizes: no sign flip
runline::Vec3 ToCoreExtent(const FVector& Half);
FBox ToUnrealBox(const runline::Aabb& B);
runline::Aabb ToCoreBox(const FBox& B);
FRotator ToUnrealRotator(float Yaw, float Pitch, float Roll = 0.0f);   // core radians -> UE degrees
```

- [ ] **Step 1: Write the failing tests.** Create `UnitsTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "RunlineUnits.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RunlineUnits;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineUnitsAxes, "Runline.Units.Axes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineUnitsAxes::RunTest(const FString&)
{
    // Right stays right, up stays up, the core's forward (-Z at yaw 0) is Unreal's +X.
    TestEqual(TEXT("core right -> UE +Y"), ToUnrealDirection({1, 0, 0}), FVector(0, 1, 0));
    TestEqual(TEXT("core up -> UE +Z"), ToUnrealDirection({0, 1, 0}), FVector(0, 0, 1));
    TestEqual(TEXT("core forward -> UE +X"), ToUnrealDirection({0, 0, -1}), FVector(1, 0, 0));
    TestEqual(TEXT("1 m -> 100 cm"), ToUnrealPosition({1, 2, 3}), FVector(-300, 100, 200));
    // Yaw: core +90 deg turns left, so it faces UE -Y.
    const FVector Fwd = ToUnrealRotator(UE_HALF_PI, 0.0f).Vector();
    TestTrue(TEXT("core yaw +pi/2 faces UE -Y"), Fwd.Equals(FVector(0, -1, 0), 1e-6));
    // Pitch: positive looks up in both.
    TestTrue(TEXT("pitch up"), ToUnrealRotator(0.0f, 0.5f).Vector().Z > 0.0);
    // The rotator's forward is the converted core forward for any yaw/pitch.
    for (float Yaw : {-2.5f, -1.0f, 0.0f, 0.3f, 1.7f, 3.0f})
        for (float Pitch : {-1.2f, 0.0f, 0.8f}) {
            const runline::Vec3 F{-std::sin(Yaw) * std::cos(Pitch), std::sin(Pitch), -std::cos(Yaw) * std::cos(Pitch)};
            TestTrue(FString::Printf(TEXT("forward at yaw %f pitch %f"), Yaw, Pitch),
                ToUnrealRotator(Yaw, Pitch).Vector().Equals(ToUnrealDirection(F), 1e-5));
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineUnitsRoundTrip, "Runline.Units.RoundTripIsExact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineUnitsRoundTrip::RunTest(const FString&)
{
    // Bit-exact both ways (float * 100 and / 100 in double recover the float).
    const runline::Vec3 Samples[] = {{2.3f, -1.5f, -146.0f}, {0.1f, 1e-4f, -0.3f}, {-200.0f, -31.0f, 120.0f}, {1.12f, 0.64f, 5.0f}};
    for (const runline::Vec3& P : Samples) {
        const runline::Vec3 Back = ToCorePosition(ToUnrealPosition(P));
        TestTrue(TEXT("position round trip"), Back.x == P.x && Back.y == P.y && Back.z == P.z);
        const runline::Vec3 E = ToCoreExtent(ToUnrealExtent(P));
        TestTrue(TEXT("extent round trip"), E.x == P.x && E.y == P.y && E.z == P.z);
    }
    const runline::Aabb B = runline::Aabb::New({-12.0f, -4.2f, -146.5f}, {12.0f, -2.6f, -146.0f});
    const runline::Aabb R = ToCoreBox(ToUnrealBox(B));
    TestTrue(TEXT("box round trip"), R.min == B.min && R.max == B.max);
    TestTrue(TEXT("UE box is valid (min < max)"), ToUnrealBox(B).Min.X < ToUnrealBox(B).Max.X);
    return true;
}
#endif
```

Run: `scripts/test-unreal.ps1 -Filter Runline.Units`. Expected: compile FAIL (`RunlineUnits.h` missing).

- [ ] **Step 2: Implement.** Create `RunlineUnits.h`:

```cpp
#pragma once
// The one place Unreal and RunlineCore coordinates meet (spec section 3, "Unit boundary").
//
// Core: metres, x right, y up, z back (right-handed); yaw 0 faces -Z, positive yaw turns left,
// positive pitch looks up. Unreal: centimetres, X forward, Y right, Z up (left-handed); positive
// yaw turns right. The map (x, y, z) -> (-z, x, y) keeps right, up and forward; it is a reflection
// (determinant -1), as a change of handedness must be. Positions round-trip exactly: a float
// times 100 and back in double recovers the float.
#include "CoreMinimal.h"
#include "RunlineCoreBridge.h"

namespace RunlineUnits
{
    inline constexpr double CmPerMetre = 100.0;

    inline FVector ToUnrealDirection(runline::Vec3 D) { return FVector(-D.z, D.x, D.y); }
    inline runline::Vec3 ToCoreDirection(const FVector& D) { return {float(D.Y), float(D.Z), float(-D.X)}; }
    inline FVector ToUnrealPosition(runline::Vec3 P) { return ToUnrealDirection(P) * CmPerMetre; }
    inline runline::Vec3 ToCorePosition(const FVector& P) { return {float(P.Y / CmPerMetre), float(P.Z / CmPerMetre), float(-P.X / CmPerMetre)}; }
    inline FVector ToUnrealVelocity(runline::Vec3 V) { return ToUnrealPosition(V); }
    inline FVector ToUnrealExtent(runline::Vec3 H) { return FVector(H.z, H.x, H.y) * CmPerMetre; }
    inline runline::Vec3 ToCoreExtent(const FVector& H) { return {float(H.Y / CmPerMetre), float(H.Z / CmPerMetre), float(H.X / CmPerMetre)}; }
    inline FBox ToUnrealBox(const runline::Aabb& B)
    {
        return FBox(FVector(-B.max.z, B.min.x, B.min.y) * CmPerMetre, FVector(-B.min.z, B.max.x, B.max.y) * CmPerMetre);
    }
    inline runline::Aabb ToCoreBox(const FBox& B)
    {
        return {{float(B.Min.Y / CmPerMetre), float(B.Min.Z / CmPerMetre), float(-B.Max.X / CmPerMetre)},
                {float(B.Max.Y / CmPerMetre), float(B.Max.Z / CmPerMetre), float(-B.Min.X / CmPerMetre)}};
    }
    inline FRotator ToUnrealRotator(float Yaw, float Pitch, float Roll = 0.0f)
    {
        return FRotator(FMath::RadiansToDegrees(double(Pitch)), FMath::RadiansToDegrees(-double(Yaw)), FMath::RadiansToDegrees(double(Roll)));
    }
}
```


- [ ] **Step 3: Run the tests.** Run `scripts/test-unreal.ps1 -Filter Runline.Units`. Expected: 2 passed. Mutation: change `ToUnrealDirection` to `FVector(D.z, D.x, D.y)` and run. Expected: `Runline.Units.Axes` FAILs. Revert.

- [ ] **Step 4: Commit.**

```bash
git add Unreal/Runline/Source/Runline
git commit -m "M2a: RunlineUnits, the single core<->Unreal conversion, with exact round trips"
```

---

### Task 5: FUnrealWorld, an Unreal broadphase with the core's narrowphase

**Files:**
- Create: `Unreal/Runline/Source/Runline/Runner/UnrealWorld.h`, `UnrealWorld.cpp`
- Create: `Unreal/Runline/Source/Runline/Tests/RunlineTestWorld.h`
- Test: `Unreal/Runline/Source/Runline/Tests/UnrealWorldTests.cpp`

**Interfaces:**
- Consumes: `runline::SweepBoxes`, `runline::OverlapsBoxes` (Task 2), `RunlineUnits` (Task 4), `replay::Replay`, `trace::Parse` (Task 2)
- Produces:

```cpp
/// The level as RunlineCore sees it, answered from an Unreal world: Unreal's overlap query finds
/// the blocking UBoxComponents near the query (the broadphase), and the core's own box maths
/// answers it (the narrowphase), so results are bit-identical to a BoxWorld of the same boxes.
class FUnrealWorld final : public runline::World
{
public:
    /// `Ignore` (usually the runner) is never seen.
    FUnrealWorld(const UWorld* InWorld, const AActor* Ignore);
    std::optional<runline::SweepHit> Sweep(runline::Vec3 Half, runline::Vec3 Start, runline::Vec3 Delta) const override;
    bool Overlaps(const runline::Aabb& Region) const override;
    /// The boxes the broadphase finds overlapping `Region` (grown by Margin), in a stable order.
    void GatherBoxes(const runline::Aabb& Region, std::vector<runline::Aabb>& Out) const;
    /// How far past the query region the broadphase looks (m): more than BoxWorld's 0.01 m hair.
    static constexpr float Margin = 0.05f;
};
```

- And `RunlineTestWorld.h` (test-only):

```cpp
/// A transient game world for adapter tests, destroyed on scope exit.
struct FRunlineTestWorld {
    FRunlineTestWorld();   // UWorld::CreateWorld(EWorldType::Game, false) + InitializeActorsForPlay
    ~FRunlineTestWorld();  // DestroyWorld(false)
    UWorld* World = nullptr;
    /// Spawns an actor with one blocking UBoxComponent exactly covering `B` (core metres).
    AActor* AddBox(const runline::Aabb& B, int32 Order);
};
```

**Ordering rule.** BoxWorld keeps list order on exact ties in t (corner hits). FUnrealWorld sorts its candidates by the `Order` stored on each box, then by min corner, so ties resolve the same way every run. Each spawned box component carries its index in the level as `ComponentTags[0] = FName(*FString::Printf(TEXT("RunlineOrder%d"), Index))`. The test world and `ARunlineCourse` (Task 7) both tag boxes this way, in level order.

- [ ] **Step 1: Write the failing tests.** Create `UnrealWorldTests.cpp` with four tests:

```cpp
#include "Misc/AutomationTest.h"
#include "RunlineTestWorld.h"
#include "UnrealWorld.h"
#include "Replay.h"
#include <fstream>
#include <sstream>

#if WITH_DEV_AUTOMATION_TESTS

namespace {
std::vector<std::vector<float>> ReadCsv(const char* Name)   // header row skipped
{
    std::ifstream In(std::string(RUNLINE_GOLDEN_DIR) + "/" + Name);
    std::vector<std::vector<float>> Rows;
    std::string Line;
    std::getline(In, Line);
    while (std::getline(In, Line)) {
        std::vector<float> R;
        std::stringstream S(Line);
        std::string Cell;
        while (std::getline(S, Cell, ',')) R.push_back(std::stof(Cell));
        Rows.push_back(R);
    }
    return Rows;
}
bool Same(float A, float B) { return std::bit_cast<uint32>(A) == std::bit_cast<uint32>(B); }
}

// 1. The M0 golden scene and sweeps: FUnrealWorld must give BoxWorld's answer bit for bit
//    (sweeps.csv covers touching, sliding along faces and starting inside).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineWorldGoldenSweeps, "Runline.World.GoldenSweeps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineWorldGoldenSweeps::RunTest(const FString&)
{
    FRunlineTestWorld TW;
    runline::BoxWorld Ref;
    const auto Scene = ReadCsv("scene.csv");
    for (int32 I = 0; I < Scene.size(); ++I) {
        const runline::Aabb B{{Scene[I][0], Scene[I][1], Scene[I][2]}, {Scene[I][3], Scene[I][4], Scene[I][5]}};
        Ref.Add(B);
        TW.AddBox(B, I);
    }
    const FUnrealWorld W(TW.World, nullptr);
    int32 Mismatches = 0;
    for (const auto& R : ReadCsv("sweeps.csv")) {
        const runline::Vec3 H{R[0], R[1], R[2]}, S{R[3], R[4], R[5]}, D{R[6], R[7], R[8]};
        const auto A = W.Sweep(H, S, D);
        const auto B = Ref.Sweep(H, S, D);
        const bool Ok = A.has_value() == B.has_value() && (!A || (Same(A->t, B->t) && A->normal == B->normal && A->point == B->point));
        if (!Ok && ++Mismatches <= 5) AddError(FString::Printf(TEXT("sweep from (%f,%f,%f) by (%f,%f,%f) differs"), S.x, S.y, S.z, D.x, D.y, D.z));
    }
    TestEqual(TEXT("sweep mismatches"), Mismatches, 0);
    return true;
}
```

Check `scene.csv`'s columns against `tools/golden/src/sweeps.rs` (`scene_csv`) before relying on `[0..5]` being min xyz, max xyz. Read the header row.

Test 2, `Runline.World.TouchingAndInside`: a floor box. A body-sized box resting exactly on it does not overlap. Swept down by 0.1 it is hit at t = 0 with normal (0,1,0). A box started half inside the floor and swept sideways ignores the floor. Compare each answer with `BoxWorld` on the same box (exact equality).

Test 3, `Runline.World.IgnoresOwnerAndTriggers`: spawn the floor, plus a second box actor directly above a downward sweep whose component is `SetCollisionEnabled(QueryOnly)` with response `ECR_Overlap` to all channels (a trigger). Spawn a third box actor passed as `Ignore`. The sweep must hit only the floor; the result equals the floor-only BoxWorld's.

Test 4, `Runline.World.EveryBoxTraceReplays`: for each `.trace` in `RUNLINE_GOLDEN_DIR/traces`, parse it with `trace::Parse` (open with `std::ifstream`). Skip traces with triangles (`!wd.tris.empty()` in any world, i.e. the mesh traces) and count them. For each trace world, spawn its boxes in a fresh `FRunlineTestWorld` (order = index). Replay with `replay::Replay(t, [&](size_t i) -> const runline::World& { return *Worlds[i]; })`, where `Worlds[i]` is an `FUnrealWorld` over that world's test world. Fail with the first divergence message. Log `"%d box traces replayed bit-exact, %d mesh traces skipped"`. Expected numbers after Task 1: 156 and 19 (19 traces have triangles today; confirm with `grep -l '^tri,' RunlineCore/tests/golden/traces/*.trace | wc -l`).

Run: `scripts/test-unreal.ps1 -Filter Runline.World`. Expected: compile FAIL (`UnrealWorld.h` missing).

- [ ] **Step 2: The test world.** Create `RunlineTestWorld.h`:

```cpp
#pragma once
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RunlineUnits.h"

/// A transient game world for adapter tests, destroyed on scope exit.
struct FRunlineTestWorld
{
    UWorld* World = nullptr;

    FRunlineTestWorld()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("RunlineTestWorld"));
        FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
        Ctx.SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        World->BeginPlay();
    }
    ~FRunlineTestWorld()
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
    /// One blocking UBoxComponent exactly covering B (core metres), tagged with its level order.
    AActor* AddBox(const runline::Aabb& B, int32 Order)
    {
        AActor* A = World->SpawnActor<AActor>();
        UBoxComponent* Box = NewObject<UBoxComponent>(A);
        const FBox U = RunlineUnits::ToUnrealBox(B);
        Box->SetBoxExtent(U.GetExtent(), false);
        Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
        Box->ComponentTags.Add(FName(*FString::Printf(TEXT("RunlineOrder%d"), Order)));
        A->SetRootComponent(Box);
        Box->RegisterComponent();
        Box->SetWorldLocation(U.GetCenter());
        return A;
    }
};
```

`ToUnrealBox` then `GetCenter`/`GetExtent` must give back exactly the core box, since both are computed in double from floats. `Runline.World.GoldenSweeps` proves it end to end. If it fails on positions only, compare `ToCoreBox(Box->Bounds.GetBox())` with `B` first.

- [ ] **Step 3: Implement FUnrealWorld.** Create `UnrealWorld.h` with the Interfaces block above, and `UnrealWorld.cpp`:

```cpp
#include "UnrealWorld.h"
#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "RunlineUnits.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineWorld, Log, All);

namespace
{
    int32 OrderOf(const UPrimitiveComponent& C)
    {
        for (const FName& Tag : C.ComponentTags) {
            const FString S = Tag.ToString();
            if (S.StartsWith(TEXT("RunlineOrder"))) return FCString::Atoi(*S.RightChop(12));
        }
        return MAX_int32;
    }
}

FUnrealWorld::FUnrealWorld(const UWorld* InWorld, const AActor* Ignore) : World(InWorld), Params(SCENE_QUERY_STAT(RunlineWorld), false)
{
    if (Ignore) Params.AddIgnoredActor(Ignore);
}

void FUnrealWorld::GatherBoxes(const runline::Aabb& Region, std::vector<runline::Aabb>& Out) const
{
    const runline::Vec3 M = runline::Vec3::Splat(Margin);
    const FBox Q = RunlineUnits::ToUnrealBox(runline::Aabb::New(Region.min - M, Region.max + M));
    TArray<FOverlapResult> Hits;
    World->OverlapMultiByObjectType(Hits, Q.GetCenter(), FQuat::Identity,
        FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllStaticObjects | FCollisionObjectQueryParams::AllDynamicObjects),
        FCollisionShape::MakeBox(Q.GetExtent()), Params);
    struct FCandidate { int32 Order; runline::Aabb Box; };
    TArray<FCandidate> Found;
    for (const FOverlapResult& H : Hits) {
        const UPrimitiveComponent* C = H.GetComponent();
        if (!C || C->GetCollisionEnabled() == ECollisionEnabled::NoCollision || C->GetCollisionEnabled() == ECollisionEnabled::ProbeOnly) continue;
        if (C->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) continue;   // triggers and the like
        const UBoxComponent* Box = Cast<UBoxComponent>(C);
        if (!Box || !C->GetComponentQuat().Equals(FQuat::Identity, 1e-6)) {
            WarnUnsupported(*C);
            continue;
        }
        const FVector Ext = Box->GetScaledBoxExtent();
        const FVector Center = Box->GetComponentLocation();
        Found.Add({OrderOf(*C), RunlineUnits::ToCoreBox(FBox(Center - Ext, Center + Ext))});
    }
    Found.Sort([](const FCandidate& A, const FCandidate& B) {
        if (A.Order != B.Order) return A.Order < B.Order;
        const runline::Vec3 a = A.Box.min, b = B.Box.min;
        return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
    });
    Out.clear();
    for (const FCandidate& F : Found) Out.push_back(F.Box);
}

std::optional<runline::SweepHit> FUnrealWorld::Sweep(runline::Vec3 Half, runline::Vec3 Start, runline::Vec3 Delta) const
{
    // The same region BoxWorld::Sweep filters candidates by; the core does the rest.
    const runline::Aabb A = runline::Aabb::New(Start - Half, Start + Half);
    std::vector<runline::Aabb> Boxes;
    GatherBoxes(A.Union(A.Translated(Delta)), Boxes);
    const runline::Aabb Region = A.Union(A.Translated(Delta));
    const runline::Aabb Grown = runline::Aabb::New(Region.min - runline::Vec3::Splat(0.01f), Region.max + runline::Vec3::Splat(0.01f));
    std::vector<runline::Aabb> Candidates;
    for (const runline::Aabb& B : Boxes)
        if (B.Overlaps(Grown)) Candidates.push_back(B);
    return runline::SweepBoxes(Candidates, Half, Start, Delta);
}

bool FUnrealWorld::Overlaps(const runline::Aabb& Region) const
{
    std::vector<runline::Aabb> Boxes;
    GatherBoxes(Region, Boxes);
    return runline::OverlapsBoxes(Boxes, Region);
}
```

Add the private members to the header: `const UWorld* World; FCollisionQueryParams Params; void WarnUnsupported(const UPrimitiveComponent& C) const;` and a `mutable TSet<TWeakObjectPtr<const UPrimitiveComponent>> Warned;`. `WarnUnsupported` logs once per component: `UE_LOG(LogRunlineWorld, Warning, TEXT("%s blocks but isn't an axis-aligned UBoxComponent; RunlineCore ignores it (M2a supports boxes only)"), *C.GetPathName())`.

Why `ECC_Pawn` decides "blocking": the runner's capsule is a Pawn. Anything set to block pawns is something she should collide with, and triggers overlap. This is the one Unreal-side collision rule.

- [ ] **Step 4: Run the tests (GREEN).** Run `scripts/test-unreal.ps1 -Filter Runline.World`. Expected: 4 passed, with the log reading `156 box traces replayed bit-exact, 19 mesh traces skipped`. A trace that diverges here but passes on the desktop is an adapter bug: an ordering tie, a missed candidate (raise `Margin`), or a box-conversion rounding error. Fix it in the adapter, never in the core.

- [ ] **Step 5: Mutation checks (read each result before committing).**
  1. Make the sort ignore `Order` (min corner only) and run. Expected: a failure in GoldenSweeps or the trace replay if any exact tie exists. If it survives, record in the ledger that no committed case has an order-dependent tie, and keep the rule anyway (faith-runner's semantics).
  2. Set `Margin = 0.0f` and run. Expected: FAIL. With zero margin, Unreal's overlap misses boxes touching the region, which BoxWorld's 0.01 hair includes.
  3. Drop the `ECC_Pawn` response check and run. Expected: `IgnoresOwnerAndTriggers` FAILs.

Revert each.

- [ ] **Step 6: Commit.**

```bash
git add Unreal/Runline/Source/Runline
git commit -m "M2a: FUnrealWorld (Unreal broadphase + core narrowphase): goldens and every box trace bit-exact in Unreal"
```

---

### Task 6: URunlineMovementComponent: fixed step, input latch, capsule, interpolated view, NotPorted guard

**Files:**
- Create: `Unreal/Runline/Source/Runline/Runner/RunlineMovementComponent.h`, `.cpp`
- Test: `Unreal/Runline/Source/Runline/Tests/MovementComponentTests.cpp`

**Interfaces:**
- Consumes: `FUnrealWorld` (Task 5), `RunlineUnits` (Task 4), `runline::Controller`
- Produces:

```cpp
UCLASS()
class URunlineMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    URunlineMovementComponent();
    static constexpr float StepSeconds = 1.0f / 60.0f;
    static constexpr int32 MaxStepsPerFrame = 5;

    /// Put the runner at a core spawn point (also its respawn point).
    void SpawnAt(runline::Vec3 Feet, float Yaw);
    /// Change the respawn point without moving (checkpoints).
    void SetRespawn(runline::Vec3 Feet, float Yaw);
    void Respawn();

    // Input, latched until the next step consumes it. Look is in core radians
    // (x positive turns left, y positive looks up).
    void SetMove(FVector2D RightForward);
    void AddLook(FVector2D YawLeftPitchUp);
    void PressJump();  void SetJumpHeld(bool bHeld);
    void PressCrouch(); void SetCrouchHeld(bool bHeld);
    void PressTurn();

    /// Run `DeltaSeconds` of game time: whole steps, the rest carried over. Public so tests can
    /// drive it without a full tick.
    void Advance(float DeltaSeconds);

    /// The eye and view to render this frame: interpolated between the last two steps, plus the
    /// look not yet stepped (so look never lags).
    FVector GetRenderEye() const;
    FRotator GetRenderRotation() const;

    const runline::Controller& Core() const { return *Controller; }
    runline::Controller& MutableCore() { return *Controller; }
    /// Steps run since play began (tests compare step counts across frame rates).
    int64 StepCount() const { return Steps; }

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
```

Design notes for the implementer:
- **CMC bypass.** `TickComponent` calls `UMovementComponent::TickComponent` (the grandparent: keeps `UpdatedComponent` bookkeeping), never `Super::TickComponent`. Then it calls `Advance(DeltaTime)`. `BeginPlay` sets `SetMovementMode(MOVE_Custom)` so no CMC physics mode is ever active. The capsule is moved with `UpdatedComponent->SetWorldLocation(..., false, nullptr, ETeleportType::TeleportPhysics)`: no sweep, because the core already resolved collision.
- **Controller lifetime.** Hold it as `TUniquePtr<runline::Controller> Controller` (or `std::unique_ptr`), created in the constructor at the origin and moved by `SpawnAt`. Hold `TUniquePtr<FUnrealWorld> CoreWorld`, created in `BeginPlay` with `(GetWorld(), GetOwner())`.
- **Latching.** `PressJump` sets `bJumpLatched = true`. Building a step's `runline::Input`:
  - `jumpPressed = bJumpLatched`;
  - `jumpHeld = bJumpHeld || bJumpLatched` (a tap between steps still counts as held for that step);
  - the same for crouch; `turnPressed = bTurnLatched`;
  - `look = PendingLook` (all of it, on the frame's first step);
  - after the step, clear the latches and `PendingLook`;
  - `moveAxis`: clamp `Move` to length 1, with glam's `clamp_length_max` order: `max * (v / sqrt(lsq))` when `lsq > 1`; `strafeRaw = FMath::Clamp(Move.X, -1, 1)`;
  - melee and takedown stay false.
- **Advance.** `Accumulator += DeltaSeconds`. Then `int32 N = FMath::Min(FloorToInt(Accumulator / StepSeconds), MaxStepsPerFrame)`. If the uncapped count exceeded the cap, drop the excess so `Accumulator < StepSeconds`. Before each step, `PrevEye = Controller->GetView().eye`. Run the step. Then sync the capsule:
  - feet → `ToUnrealPosition(feet) + FVector(0, 0, HalfHeightCm)`;
  - capsule radius `halfWidth * 100`, half height `Height() * 50`, set with `SetCapsuleSize` only when it changed;
  - `Velocity = ToUnrealVelocity(vel)`, so animation and audio can read CMC's velocity.
- **Render view.** `Alpha = Accumulator / StepSeconds`. Eye = `ToUnrealPosition(Lerp(PrevEye, CurEye, Alpha))`. Rotation = `ToUnrealRotator(yaw + PendingLook.X, FMath::Clamp(pitch + PendingLook.Y, -1.5f, 1.5f), view.roll)`. Use the same pitch limit as `Controller::Step`'s `FClamp(pitch + input.look.y, -1.5f, 1.5f)`; M2b replaces it with LookLimiter.
- **Guard.** Wrap each `Controller->Step` in `try { ... } catch (const runline::NotPorted& E) { ... }`. On catch: `UE_LOG(LogRunlineMovement, Error, TEXT("RunlineCore: %hs; respawning"), E.what())` the first time a given message is seen, then `Controller->Respawn()`. Count guards in `int32 NotPortedCount` (public read accessor `NotPortedCount()`, for the test).

- [ ] **Step 1: Write the failing tests.** `MovementComponentTests.cpp` spawns, in an `FRunlineTestWorld`, a floor box `{-50,-1,-50}..{50,0,50}` and an `ACharacter` subclass whose CMC is `URunlineMovementComponent`:

```cpp
// in the test file, test-only:
UCLASS() class ARunlineTestRunner : public ACharacter { GENERATED_BODY()
public: ARunlineTestRunner(const FObjectInitializer& OI)
    : Super(OI.SetDefaultSubobjectClass<URunlineMovementComponent>(ACharacter::CharacterMovementComponentName)) {} };
```

(UHT only parses headers, so this class lives in `Tests/RunlineTestRunner.h` with `#include "RunlineTestRunner.generated.h"`. It ships in the module: three lines, harmless.)

Tests, each calling `Advance` directly (no engine tick):

1. `Runline.Movement.MatchesCoreDirect`: spawn at (0,0,0), yaw 0. Advance 1/60 with `SetMove({0,1})` for 120 frames. A bare `runline::Controller` on a `BoxWorld` with the same floor, stepped 120 times with the same `Input`, must equal it exactly (`feet`, `vel`, `yaw`, state index). The capsule location must equal `ToUnrealPosition(feet) + (0,0,HalfHeightCm)`.
2. `Runline.Movement.FrameRates` (Review Focus 1): three runners, run forward with jump held, at frame dt 1/30, 1/60 and 1/144 for 2 s of game time. Each reaches the same `StepCount()` (120, give or take one step of accumulator remainder). At equal step counts the core states are identical: compare after advancing each until `StepCount() == 118`.
3. `Runline.Movement.Hitch`: one `Advance(0.5f)` runs exactly `MaxStepsPerFrame` steps and leaves `Accumulator < StepSeconds` (expose `GetAccumulator()` for the test).
4. `Runline.Movement.InputLatch` (Review Focus 2):
   - At 144 fps: `PressJump(); SetJumpHeld(true);` advance 1/144, `SetJumpHeld(false)`, then advance 1/144 three more times. Exactly one `Jump` event across the steps run, and the runner left the ground.
   - At 30 fps: `PressJump()` then one `Advance(1/30)` runs two steps. Only the first step's input has `jumpPressed`. Check with a spy: expose `LastInputs()` (a `TArray<runline::Input>` of the frame's step inputs, test-only).
5. `Runline.Movement.NotPortedGuard` (Review Focus 4): `MutableCore().tuning.autoStepUp = true` (TryAutoStepUp throws), walk forward 30 frames. No crash, `NotPortedCount() > 0`, and the runner is back at its spawn after the guard.
6. `Runline.Movement.LandOnYourBack`: run 2 s forward, jump, press turn once airborne and falling, as in faith-runner's `air_turn_lands_you_on_your_back`. `LandOnBack` is among the events, `NotPortedCount() == 0`, and pressing jump then waiting `layOnGroundGetUpTime` returns to Ground.

Run: `scripts/test-unreal.ps1 -Filter Runline.Movement`. Expected: compile FAIL.

- [ ] **Step 2: Implement the component** from the Interfaces block and design notes. Keep it under about 250 lines. If it grows, split input latching into a plain struct `FRunlineInputLatch` (in the same header) with its own test.

- [ ] **Step 3: Run (GREEN).** Run `scripts/test-unreal.ps1 -Filter Runline.`. Expected: all Runline.* pass. The desktop suite (`scripts/test.ps1`) is untouched and still passes.

- [ ] **Step 4: Mutation checks (read each result).**
  1. Feed `look` to every step of the frame, not just the first. Expected: a FrameRates or InputLatch failure if any test looks; if none do, add a look assertion to FrameRates (`AddLook` at 30 fps changes yaw by exactly the look once).
  2. Remove `|| bJumpLatched` from `jumpHeld`. Expected: InputLatch FAILs (a tapped jump gets `held = false`, and the jump buffer may drop it).
  3. Remove the cap. Expected: Hitch FAILs.

Revert each.

- [ ] **Step 5: Commit.**

```bash
git add Unreal/Runline/Source/Runline
git commit -m "M2a: URunlineMovementComponent: fixed 60 Hz steps, input latch, capsule sync, interpolated view, NotPorted guard"
```

---

### Task 7: Courses from faith-runner's levels, the runner pawn, input and camera (playable)

**Files:**
- Create: `tools/golden/src/levels.rs`; modify `tools/golden/src/main.rs`
- Create: `Unreal/Runline/Data/Levels/greybox.csv`, `rooftops.csv` (generated)
- Create: `Unreal/Runline/Source/Runline/Runner/RunlineCourse.h/.cpp`, `RunlineRunner.h/.cpp`, `RunlineRunGameMode.h/.cpp`
- Test: `Unreal/Runline/Source/Runline/Tests/CourseTests.cpp`
- Modify: `CREDITS.md` (the two courses are faith-runner's level designs)

**Interfaces:**
- Level CSV (core metres), one record per line:

```
level,<name>
solid,<look>,minx,miny,minz,maxx,maxy,maxz        # collides; look = Roof|Wall|Runner|Prop|Finish|Skyline
decor,<look>,minx,miny,minz,maxx,maxy,maxz        # drawn only
checkpoint,<name>,minx,miny,minz,maxx,maxy,maxz,spawnx,spawny,spawnz,yaw
finish,minx,miny,minz,maxx,maxy,maxz
skipped_fixtures,<count>                           # ziplines, poles, beams: M4
```

- Produces:

```cpp
/// A faith-runner course built at runtime from Data/Levels/<name>.csv: one blocking box per solid
/// (tagged with its level order), a cube mesh per box for looks, a sun and sky light.
UCLASS() class ARunlineCourse : public AActor { GENERATED_BODY()
public:
    bool Load(const FString& LevelName);           // false (with an error log) if the file is missing/bad
    int32 SolidCount() const;
    struct FCheckpoint { FString Name; runline::Aabb Region; runline::Vec3 Spawn; float Yaw; };
    const TArray<FCheckpoint>& Checkpoints() const;
    /// Index of the furthest checkpoint containing `Feet`, or INDEX_NONE.
    int32 CheckpointAt(runline::Vec3 Feet) const;
};
UCLASS() class ARunlineRunner : public ACharacter;       // camera + Enhanced Input built in code
UCLASS() class ARunlineRunGameMode : public AGameModeBase;   // DefaultPawnClass = ARunlineRunner;
                                                             // spawns ARunlineCourse from ?course= (default greybox)
```

- [ ] **Step 1: Export the levels (RED for the course test).** Create `tools/golden/src/levels.rs`. It writes `faith_move::greybox::greybox()` and `faith_move::rooftops::rooftops()` in the format above, to `../../Unreal/Runline/Data/Levels/` relative to the golden tool, using the existing `common::write` helper, with floats via `{}` (shortest round-trip). Look names come from `{:?}` of `Look`. Add `mod levels;` and `levels::write_all();` to main.rs. Run: `cargo run --release` in tools/golden (from a clean faith-runner checkout, per CLAUDE.md). Then `git status`: the golden CSVs must be unchanged and the two level files new.

Write `CourseTests.cpp`, test `Runline.Course.LoadsBothLevels`. In an `FRunlineTestWorld`, spawn an `ARunlineCourse`, load `greybox`, then in a second world `rooftops`. Each loads, `SolidCount()` equals the number of `solid,` lines in its CSV, and `Checkpoints().Num() > 0`. Rooftops' first checkpoint is named `R1 Start`. Every solid's collision box equals the CSV box (`ToCoreBox` of the component bounds, exact). Then: a `runline::Controller` stepped on an `FUnrealWorld` over the greybox course, spawned at checkpoint 0 and held forward for 60 steps, never falls below the spawn y minus 0.5 m (she runs on the roof). Run it. Expected: compile FAIL.

- [ ] **Step 2: ARunlineCourse.**
  - `Load` reads `FPaths::ProjectDir() / TEXT("Data/Levels") / LevelName + TEXT(".csv")` with `FFileHelper::LoadFileToStringArray`.
  - For each `solid`, it adds a `UBoxComponent` (BlockAll, tag `RunlineOrder<i>`, attached to the root), positioned and sized via `ToUnrealBox`.
  - It adds a `UStaticMeshComponent` with `/Engine/BasicShapes/Cube.Cube` (100 cm), scaled to the box size / 100, `NoCollision`, and colour by look, through a dynamic material instance of `/Engine/BasicShapes/BasicShapeMaterial` with parameter `Color`. Palette: Roof light grey, Wall mid grey, Runner red (Mirror's Edge runner vision), Prop dark grey, Finish orange, Skyline pale blue-grey.
  - `decor` boxes get only the mesh.
  - It spawns an `ADirectionalLight` (pitch −50°, intensity 10 lux) and an `ASkyLight` (`bRealTimeCapture = true`), plus `ASkyAtmosphere`, so an empty map is lit.
  - `skipped_fixtures` > 0 logs a warning naming the count (M4).

- [ ] **Step 3: ARunlineRunner.**
  - The constructor uses `Super(OI.SetDefaultSubobjectClass<URunlineMovementComponent>(ACharacter::CharacterMovementComponentName))`.
  - A `UCameraComponent` with `bUsePawnControlRotation = false`, attached to the root.
  - `Tick` (tick group `TG_PostPhysics`, with the movement component as a prerequisite) sets the camera's world location and rotation from `GetRenderEye()`/`GetRenderRotation()`, and calls `SetControlRotation(GetRenderRotation())` on its controller, so engine systems agree.
  - Each tick it also checks `Course->CheckpointAt(feet)`: a higher index than the current one becomes the respawn point via `SetRespawn`, logged on screen with `GEngine->AddOnScreenDebugMessage`, as faith-runner's "CHECKPOINT - name" flash.
  - **Input in code**, in `SetupPlayerInputComponent`: create `UInputAction`s with `NewObject<UInputAction>(this)`. `ValueType` is `Axis2D` for Move, MouseLook and PadLook, and `Boolean` for Jump, Crouch, Turn and Respawn. Create a `UInputMappingContext` mapping the keys in the input table. W/S/A/D map to Move with `UInputModifierSwizzleAxis` (YXZ) for W/S and `UInputModifierNegate` for S/A, which is the template's pattern.
  - Add the context in `PawnClientRestart` via the local player's `UEnhancedInputLocalPlayerSubsystem` (priority 1).
  - Bind:
    - Move: `Triggered` and `Completed` → `SetMove`.
    - MouseLook: `Triggered` → `AddLook(FVector2D(-V.X * 0.0022f, V.Y * 0.0022f))`.
    - PadLook: `Triggered` → `AddLook(FVector2D(-V.X, V.Y) * 3.2f * GetWorld()->GetDeltaSeconds())`.
    - Jump: `Started` → `PressJump` + `SetJumpHeld(true)`, `Completed` → `SetJumpHeld(false)`. Crouch the same way.
    - Turn: `Started` → `PressTurn`. Respawn: `Started` → `Respawn`.
  - Expose the sensitivities as `UPROPERTY(EditAnywhere, Category="Runline|Input") float MouseRadiansPerCount = 0.0022f; float PadRadiansPerSecond = 3.2f;`.
- [ ] **Step 4: ARunlineRunGameMode.** `DefaultPawnClass = ARunlineRunner::StaticClass()`. `InitGame` reads `UGameplayStatics::ParseOption(Options, TEXT("course"))` (default `greybox`). `StartPlay` spawns the course, calls `Load`, and moves the runner there with `SpawnAt(checkpoint 0 spawn, yaw)` via `RestartPlayer` + the pawn's component. No config change: the template maps keep their own game modes, and Runline's mode is chosen per launch (Step 6).

- [ ] **Step 5: Run the tests.** Run `scripts/test-unreal.ps1` (all Runline.*). Expected: all pass. Run `scripts/test.ps1`. Expected: desktop suite unchanged.

- [ ] **Step 6: Launch it, then hand to the user for the playtest checklist.** Launch command (add it to CLAUDE.md):

```powershell
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\Unreal\Runline\Runline.uproject" '/Engine/Maps/Entry?game=/Script/Runline.RunlineRunGameMode?course=rooftops' -game -windowed -ResX=1600 -ResY=900
```

Check by launching once yourself with `-ExecCmds="HighResShot 1"` and opening the screenshot from `Saved/Screenshots`: the course is lit, coloured, and seen from the R1 start. The user then checks, and each item's result goes in the ledger:
  1. Mouse right turns right; mouse up looks up (or the sign in Step 3 is flipped).
  2. W runs forward along −Z core, the course direction. A/D strafe the right way.
  3. Space jumps; Shift slides at speed; Q turns 180 and Q in the air lands you on your back; jump gets you up.
  4. A wallrun on the R2 billboard; a vault over the R1 AC units; the R3 climb with a ledge grab and pull-up.
  5. Falling to the street respawns at the last checkpoint; R respawns.
  6. No stutter at 144 Hz or more: turning is smooth (look isn't stepped), and the world doesn't jitter (camera interpolation).
  7. The pad works the same (left stick, right stick, A, B, Y).

- [ ] **Step 7: Commit.**

```bash
git add tools/golden/src Unreal/Runline/Data Unreal/Runline/Source CREDITS.md CLAUDE.md
git commit -m "M2a: playable runner: faith-runner courses spawned from data, Enhanced Input in code, interpolated camera"
```

---

## Self-review notes (done while writing)

- **Spec coverage, M2 row:**
  - component: Task 6.
  - FUnrealWorld: Task 5.
  - Enhanced Input, keyboard and controller: Task 7.
  - CameraFx and arms: **M2b**.
  - Rooftops "imported by script": runtime import in Task 7; the baked `.umap` is **M2c**.
  - §3 axis mapping written down: the table above, tested in Task 4.
  - Fixed step and unstepped look: Task 6.
  - §4.4 Unreal automation tests through the real component: Tasks 3, 5 and 6.
  - §6 "FUnrealWorld vs BoxWorld on the same boxes": Task 5, made bit-exact by design.
- **Placeholders:** none. Task 7's playtest is a manual check by design (spec §4.5: feel is checked only by playtesting).
- **Type consistency:**
  - `SweepBoxes`/`OverlapsBoxes` (Task 2) are used in Task 5.
  - `replay::Replay(t, worldFor)` (Task 2) is used in Task 5.
  - `RunlineUnits::*` (Task 4) is used in Tasks 5, 6 and 7.
  - `FRunlineTestWorld::AddBox(Aabb, int32)` (Task 5) is used in Tasks 6 and 7.
  - `URunlineMovementComponent::SpawnAt/SetRespawn/Respawn/Advance/GetRenderEye/GetRenderRotation` (Task 6) is used in Task 7.
- **Known risks for the executor:**
  - The exact automation log wording (Task 3, Step 4).
  - `UWorld::CreateWorld` world-context setup varies across engine versions. If `InitializeActorsForPlay` asserts, use `FAutomationEditorCommonUtils::CreateNewMap()` in an editor-context test instead.
  - `/Engine/BasicShapes/BasicShapeMaterial` may not expose a `Color` parameter. Check it in the editor's material, or fall back to `/Engine/EngineMaterials/DefaultMaterial` with per-look `UMaterialInstanceDynamic` vector parameter `Color` from a small material created in M2c.
