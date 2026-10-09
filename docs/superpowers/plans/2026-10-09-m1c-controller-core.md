# M1c Controller Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the heart of `controller.rs`: the types, `Controller::new`, `step`, `substep`, the `Ground` and `Air` states, jumping, landing (into `Ground` or the hard landing), the hard landing itself, `integrate`, and the view. Prove it by **replaying faith-runner's own recorded tests** frame by frame, bit-exact. The five M1b traces (falls and lands, sprint build-up, jump arc, jump landing boost, hard landing) must replay with zero divergence.

**Architecture:**
- **Port style.** `RunlineCore/include/runline/Controller.h` and `src/Controller.cpp` port the Rust function by function, in source order, keeping its names (camelCase) and comments.
- **States.** `State` is a `std::variant` of per-state structs, mirroring Rust's enum. M1c includes only the alternatives the core can reach: `Ground`, `Air`, `Slide`, `Roll`, `Stunned`, `SoftLand`, `LayOnGround`, `WallClimb`, `LedgeHang`, `WallRun`. Later milestones add the rest.
- **Stubs.** Every check or move this milestone doesn't port is an explicit stub in `src/ControllerStubs.cpp`, marked `// NOT PORTED: <rust function> (<milestone>)`.
  - A **check** stub returns the "nothing here" answer (`false` or `std::nullopt`).
  - A **state** handler stub throws `NotPorted`.
- **The replay harness** (`tests/replay_tests.cpp`) proves the stubs are safe on the selected traces. Where the real Rust check *would* have fired, the replay diverges at that frame; where a stubbed state is reached, `NotPorted` names it.

**Tech Stack:** C++20 / MSVC `/fp:strict`, doctest, the M1b trace loader, faith-runner `9470d755` plus the trace patch.

**Spec:** `docs/superpowers/specs/2026-10-09-faith-movement-port-design.md` (M1, third part; spec §4.2: state and events exact, first divergence reported).

## Global Constraints

Everything from M1a and M1b still applies:
- C++20, no Unreal; metres, Y up; `float` throughout.
- `FMin`/`FMax`/`FClamp` only; every literal carries `f`.
- Golden and replay comparisons are **bit-exact**.
- ASCII sources.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

New for M1c:
- **glam helpers used by the controller are ported exactly from glam 0.32.1's source** (`%USERPROFILE%\.cargo\registry\src\index.crates.io-*\glam-0.32.1\src\f32\vec3.rs`, and `vec2.rs` for `Vec2`): `clamp_length_max`, `normalize_or`, and `Vec2` clamping. **Read the source before porting each one**; never port from memory.
- **Rust `f32` methods map to the CRT** the same way rustc uses it on `x86_64-pc-windows-msvc`: `sin`/`cos`/`exp`/`powf`/`sqrt` become `std::sin`/`std::cos`/`std::exp`/`std::pow`/`std::sqrt` on `float`. `to_radians` is `x * (kPi / 180.0f)`. `rem_euclid` is ported from Rust's definition when it's needed.
- **Porting convention:** `controller.rs` is about 2,800 lines, so this plan names the line ranges to port and the stub list instead of re-typing the code. The replay test is the gate; it reports the first frame and field that differ.

## Review Focus

1. **Substepping:** `dt` is split into 1/120 s substeps (capped at 0.1 s), and edge-triggered inputs (`jump_pressed` and the other presses, plus `look`) count only on the first substep. An off-by-one here shifts every jump. Pinned by every replay (the jump traces press jump on frame 0).
2. **Input normalisation:** `strafe_raw` is taken from `move_axis.x` when it's 0, and `move_axis` is clamped to length 1 (glam `clamp_length_max`). Pinned by a unit test with a diagonal over-long stick, plus the sprint replay.
3. **Coyote and buffered jumps:** a jump pressed just before landing is buffered (`jump_buffer`) and fires on touchdown. Pinned by the `jump_landing_hands_back_the_boost` replay, which jumps repeatedly.
4. **Landing classification by fall height** (`fall_peak - body_y`): a normal landing, a hard landing (5.3 m) or a lethal fall (10 m). Pinned by the `falls_and_lands` and `hard_landing_stops_dead` replays, and a unit test for the lethal height.
5. **A stub that silently answers wrong:** each stub is listed in the ledger with the Rust function it replaces. The replay tests fail on the first frame a stub's answer would have mattered.

---

## File Structure

```
RunlineCore/include/runline/Controller.h   Input, Event, State variants, View, Controller
RunlineCore/src/Controller.cpp             ported functions (controller.rs, source order)
RunlineCore/src/ControllerStubs.cpp        NOT PORTED stubs, one per Rust function, with the milestone that ports it
RunlineCore/tests/controller_tests.cpp     unit tests (types, new, respawn, input normalisation, lethal fall)
RunlineCore/tests/replay_tests.cpp         replays every committed trace frame by frame
```

---

### Task 1: Controller types, construction and the replay harness (tests first)

**Files:**
- Create: `RunlineCore/include/runline/Controller.h`, `RunlineCore/src/Controller.cpp`, `RunlineCore/src/ControllerStubs.cpp`, `RunlineCore/tests/controller_tests.cpp`, `RunlineCore/tests/replay_tests.cpp`
- Modify: `RunlineCore/CMakeLists.txt`

**Interfaces:**
- Consumes: `Tuning`, `Locomotion`, `RunState`, `RunInput`, `World`, `Body`, `MoveAxis`, `SlideMove`, the probes, and `trace::` (M1a/M1b).
- Produces:
  - `struct Vec2 { float x, y; }`, with glam's `clamp_length_max`
  - `struct Input` (`controller.rs:20-37`, camelCase)
  - `enum class EventKind` (every variant of `controller.rs:220-311`, in order) and `struct Event { EventKind kind; float impact, fall; Vec3 dir; }`, with `const char* EventName(EventKind)` returning the Rust variant name
  - `struct View { Vec3 eye; float yaw, pitch, roll, fovDeg; }`
  - `using State = std::variant<Ground, Air, Slide, Roll, Stunned, SoftLand, LayOnGround, WallRun, WallClimb, LedgeHang>`, each a struct with the Rust payload fields
  - `const char* StateName(const State&)` returning `State::name()`'s strings
  - `struct NotPorted : std::logic_error`
  - `class Controller`, with public fields `tuning, feet, vel, yaw, pitch, state, crouched, sprintCharge, spawn, spawnYaw, events` and the private fields the ported functions use, plus `Controller(Tuning, Vec3 spawn, float spawnYaw)`, `Step(float dt, const Input&, const World&)`, `Respawn()`, `Height()`, `GetBody()`, `HorizontalSpeed()`, `Forward()` and `GetView()`

- [ ] **Step 1: Write the failing unit tests, `RunlineCore/tests/controller_tests.cpp`**

```cpp
#include <doctest/doctest.h>
#include "runline/BoxWorld.h"
#include "runline/Controller.h"
using namespace runline;

namespace {
BoxWorld Floor() {
    BoxWorld w;
    w.Add(Aabb::New({-50, -1, -50}, {50, 0, 50}));
    return w;
}
} // namespace

TEST_CASE("Controller::new starts in the air at the spawn, facing spawn_yaw") {
    const Controller c(Tuning{}, {1, 2, 3}, 0.5f);
    CHECK(c.feet == Vec3{1, 2, 3});
    CHECK(c.vel == Vec3::Zero());
    CHECK(c.yaw == 0.5f);
    CHECK(std::holds_alternative<Air>(c.state));
    CHECK(std::string(StateName(c.state)) == "Air");
    CHECK(c.Height() == Tuning{}.standHeight);
}

TEST_CASE("state and event names match faith_move's") {
    CHECK(std::string(StateName(State{Stunned{0.0f}})) == "Hard landing");
    CHECK(std::string(StateName(State{LedgeHang{{1, 0, 0}, 1.0f, true}})) == "Ledge hang (turned)");
    CHECK(std::string(EventName(EventKind::Land)) == "Land");
    CHECK(std::string(EventName(EventKind::HardLand)) == "HardLand");
}

TEST_CASE("an over-long diagonal stick is clamped to length 1 before it moves you") {   // Review Focus 2
    const auto w = Floor();
    Controller a(Tuning{}, {0, 0, 0}, 0.0f), b(Tuning{}, {0, 0, 0}, 0.0f);
    a.state = Ground{};
    b.state = Ground{};
    Input big, unit;
    big.moveAxis = {3.0f, 3.0f};
    unit.moveAxis = {0.70710677f, 0.70710677f};
    unit.strafeRaw = 1.0f;   // what Step derives from big's x (clamped to 1)
    for (int i = 0; i < 30; ++i) {
        a.Step(1.0f / 60.0f, big, w);
        b.Step(1.0f / 60.0f, unit, w);
    }
    CHECK(a.feet == b.feet);
}

TEST_CASE("a fall of 10 m or more is lethal and respawns you") {                        // Review Focus 4
    BoxWorld w;
    w.Add(Aabb::New({-50, -21, -50}, {50, -20, 50}));
    Controller c(Tuning{}, {0, 0, 0}, 0.0f);
    bool died = false;
    for (int i = 0; i < 300 && !died; ++i) {
        c.Step(1.0f / 60.0f, Input{}, w);
        for (const Event& e : c.events) died |= e.kind == EventKind::Death;
    }
    CHECK(died);
    CHECK(c.feet == c.spawn);   // respawn_on_death is on by default
}
```

- [ ] **Step 2: Write the replay harness, `RunlineCore/tests/replay_tests.cpp`**

```cpp
#include "Trace.h"
#include <bit>
#include <cstdint>
#include <sstream>
#include "runline/Controller.h"
using namespace runline;

namespace {

bool Same(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

std::string Num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

/// The state a trace starts from: Controller::new at the recorded feet and yaw, then the fields
/// tests set before their first step.
State StartState(const std::string& name) {
    if (name == "Ground") return Ground{};
    if (name == "Air") return Air{};
    throw std::runtime_error("trace starts in state " + name + ", which replay can't set up");
}

/// First difference between a recorded snapshot and the controller, or "".
std::string Diff(const trace::Snapshot& want, const Controller& c, const std::vector<std::string>& wantEvents) {
    std::ostringstream d;
    const auto cmp = [&](const char* f, float w, float g) { if (d.str().empty() && !Same(w, g)) d << f << ": want " << Num(w) << ", got " << Num(g); };
    cmp("feet.x", want.feet.x, c.feet.x); cmp("feet.y", want.feet.y, c.feet.y); cmp("feet.z", want.feet.z, c.feet.z);
    cmp("vel.x", want.vel.x, c.vel.x); cmp("vel.y", want.vel.y, c.vel.y); cmp("vel.z", want.vel.z, c.vel.z);
    cmp("yaw", want.yaw, c.yaw); cmp("pitch", want.pitch, c.pitch);
    cmp("sprint_charge", want.sprintCharge, c.sprintCharge);
    if (d.str().empty() && want.state != StateName(c.state)) d << "state: want " << want.state << ", got " << StateName(c.state);
    if (d.str().empty() && want.crouched != c.crouched) d << "crouched: want " << want.crouched << ", got " << c.crouched;
    if (d.str().empty()) {
        std::vector<std::string> got;
        for (const Event& e : c.events) got.push_back(EventName(e.kind));
        if (got != wantEvents) {
            d << "events: want [";
            for (const auto& e : wantEvents) d << e << ' ';
            d << "], got [";
            for (const auto& e : got) d << e << ' ';
            d << ']';
        }
    }
    return d.str();
}

/// Replays a trace; returns "" if every frame matches, else where and how it first diverged.
std::string Replay(const trace::Trace& t) {
    if (!t.defaultTuning) return "uses non-default tuning (not replayable yet)";
    Controller c(Tuning{}, t.init.feet, t.init.yaw);
    c.vel = t.init.vel;
    c.pitch = t.init.pitch;
    c.state = StartState(t.init.state);
    c.crouched = t.init.crouched;
    c.sprintCharge = t.init.sprintCharge;
    for (size_t i = 0; i < t.frames.size(); ++i) {
        const trace::Frame& f = t.frames[i];
        const auto& wd = t.worlds[f.world];
        if (!wd.fixtures.empty()) return "frame " + std::to_string(i) + ": fixtures not ported yet";
        Input in;
        in.moveAxis = {f.in.moveX, f.in.moveY};
        in.look = {f.in.lookX, f.in.lookY};
        in.jumpPressed = f.in.jumpPressed; in.jumpHeld = f.in.jumpHeld;
        in.crouchPressed = f.in.crouchPressed; in.crouchHeld = f.in.crouchHeld;
        in.turnPressed = f.in.turnPressed; in.meleePressed = f.in.meleePressed;
        in.takedownPressed = f.in.takedownPressed; in.strafeRaw = f.in.strafeRaw;
        try {
            if (wd.tris.empty()) c.Step(f.in.dt, in, trace::Boxes(wd));
            else c.Step(f.in.dt, in, trace::Mesh(wd));
        } catch (const NotPorted& e) {
            return "frame " + std::to_string(i) + ": reached " + e.what();
        }
        const std::string d = Diff(f.out, c, f.events);
        if (!d.empty()) return "frame " + std::to_string(i) + " (of " + std::to_string(t.frames.size()) + "): " + d;
    }
    return "";
}

} // namespace

TEST_CASE("replay: falls_and_lands") { CHECK(Replay(trace::Load("tests__falls_and_lands__1")) == ""); }
TEST_CASE("replay: sprint_builds_up") { CHECK(Replay(trace::Load("tests__sprint_builds_up__1")) == ""); }
TEST_CASE("replay: jump_arc_and_land") { CHECK(Replay(trace::Load("tests__jump_arc_and_land__1")) == ""); }
TEST_CASE("replay: jump_landing_hands_back_the_boost") { CHECK(Replay(trace::Load("tests__jump_landing_hands_back_the_boost__1")) == ""); }
TEST_CASE("replay: hard_landing_stops_dead") { CHECK(Replay(trace::Load("tests__hard_landing_stops_dead__1")) == ""); }
```

Rebuilding the world every frame is fine at these sizes. Add `#include <cstdio>` for `snprintf`.

Add `src/Controller.cpp` and `src/ControllerStubs.cpp` to the library, and `tests/controller_tests.cpp` and `tests/replay_tests.cpp` to the tests, in `RunlineCore/CMakeLists.txt`. Create placeholder files containing only an `#include` or `#pragma once`.

- [ ] **Step 3: Run and check they fail to build**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: build errors, `Controller` undeclared.

- [ ] **Step 4: Port the types and construction**

Into `Controller.h` and `Controller.cpp`, port:
- **Input:** `Input` (`controller.rs:20-37`), with a minimal `Vec2`. Port `clamp_length_max` from glam's `vec2.rs`.
- **Events:** `Event` and its variant names (`:220-311`). `EventName` returns exactly the Rust variant identifier, because the trace patch records `format!("{:?}")` up to the first non-identifier character.
- **States:** the M1c state alternatives, with their payloads (`:111-171`), and `StateName` (`:173-218`).
- **The controller:** `Takeoff`, the `Controller` fields used by the ported functions (`:441-570`), `new` (`:579-650`), `respawn` (`:663-688`), `height`/`body_y`/`body`/`horizontal_speed`/`forward` (`:690-716`), the free helpers `forward`/`right`/`horiz`/`uu_per_s`/`uu`/`smoothstep` (`:571-577` region), and `View`/`view`/`root`.

Fields that only unported features use (melee targets, doors, zipline, takedowns and so on) are left out until those features arrive. Record each omission in the ledger.

- [ ] **Step 5: Port the core loop, with stubs for everything else**

Port into `Controller.cpp`, in source order:
- **The step:** `step` (`:718-875`) and `substep` (`:1082-1144`).
- **Small helpers:** `sync_sprint_charge`, `headroom`, `grounded`, `try_stand`, `update_coil`, `uncoil_on_ground` (`:1005-1080`).
- **Ground and jumping:** `ground` (`:1146-1249`), `ground_jump`, `dodge_jump`, `end_dodge`, `dodge` and `jump` (`:1251-1305`).
- **Air:** `air` (`:1424-1491`), `update_soft_brace` and `air_transitions` (`:1493-1624`). Within `air_transitions`, port the wallclimb start, the ledge-grab check and the wallrun entry; stub `try_grab_fixture`, `vault::plan` and `find_wallrun`.
- **Landing:** `grab_ledge` if it's short, otherwise stub it (it leads to `LedgeHang`); `land` (`:1722-1808`); `soft_below`; `stunned` (`:2028-2036`).
- **Movement and view:** `integrate`, `offset_mesh`, `smooth_mesh` and `update_view` (`:2692-2792`); `check_against_wall` (`:877-893`); `check_barbed_wire` (`:1976-1997`, a no-op without fixtures, but ported).

In `ControllerStubs.cpp`, stub with the "nothing here" answer:
- **Returning `false`:** `try_into_climb`, `try_rump_slide`, `try_balance`, `try_auto_step_up`, `try_springboard`, `try_vertigo`, `try_grab_fixture`, `can_turn`
- **Returning `nullopt`:** `find_wallrun`, and `vault::plan` (as a free function)
- **No-ops** (they only act when a melee, vertigo or takedown is under way, which nothing ported can start): `vertigo_input`, `melee_tick`, `melee_pressed_again`, `start_melee`, `try_takedown`
- **Throwing `NotPorted("<state>")`:** the state handlers for `Slide`, `Roll`, `SoftLand`, `LayOnGround`, `WallRun`, `WallClimb` and `LedgeHang`
- **Throwing `NotPorted("turn")`:** `start_turn`

Each stub's comment names the Rust function and the milestone that ports it (M1d for slide, roll and wall moves; M4 for fixtures and the rest).

Record every stub in the ledger as `Task 1: stub <fn> -> <answer> (ported in <milestone>)`.

- [ ] **Step 6: Run until every replay passes**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 71 test cases (62 + 4 unit + 5 replay).

While a replay fails, its message names the first divergent frame and field. Use superpowers:systematic-debugging: compare that function's port against the Rust line by line around the frame's state. Never change a test's expectation to make it pass. If a replay reaches `NotPorted`, the stubbed function is needed: port it (and record that), rather than widening a stub.

- [ ] **Step 7: Prove the replay catches mistakes**

Mutation checks (revert after each):
1. In `step`, let `jump_pressed` through on every substep (drop it from the `!first` reset). Expected: the jump replays FAIL (Review Focus 1).
2. In `land`, change the hard-landing comparison `fall >= hardLandHeight` to `>`. If that survives because no fall lands exactly on the threshold, change the threshold to `hardLandHeight * 1.5f` instead. Expected: `hard_landing_stops_dead` FAILS.
3. In `ground`, change the jump-buffer check `jumpBuffer > 0.0f` to `jumpBuffer > 0.05f`. Expected: `jump_landing_hands_back_the_boost` FAILS (Review Focus 3).

Revert and re-run: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add RunlineCore
git commit -m "M1c: port the controller core (step, ground, air, jump, land, integrate), replay-checked against faith-runner

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Done when

- `scripts/test.ps1` prints SUCCESS, with 71 test cases, and all five replays match every frame bit for bit.
- Every stub is listed in the ledger with its Rust function and target milestone.
- The three mutation checks failed as expected and were reverted.

**Next (M1d):** port slide, roll, the wallrun, the wallclimb, the ledge hang and pull-up, and the 180 turn and kick. Add their traces to `tools/trace/selected.txt` (`tests__slide_*`, `tests__wallrun_*`, `tests__wallclimb_*`, `tests__hang_*`, `tests__hard_landing_vs_roll` and others), and turn each stub into a real port.
