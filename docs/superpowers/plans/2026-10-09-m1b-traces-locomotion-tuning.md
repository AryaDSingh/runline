# M1b Traces, Locomotion and Tuning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the trace pipeline M1c will port the controller against, using option A: faith-runner's own tests are recorded through a local patch that is never committed to faith-runner. Then port the two pieces the controller stands on first, `locomotion.rs` (the ground acceleration and sprint curve) and `tuning.rs` (every feel number), both checked bit-exact against Rust.

**Architecture:**
- **Traces.** A small patch, stored in this repo at `tools/trace/faith-runner-trace.patch`, is applied to a **separate git worktree** of the reference clone (`../faith-runner-trace`). The reference clone stays clean, so the golden tool's REFERENCE check keeps working. The patch adds a `World::dump()` (geometry as text) and wraps `Controller::step` so that, when `FAITH_TRACE_DIR` is set, every step writes its input and resulting state to a per-test `.trace` file.
- **Why geometry and state, not world queries.** The C++ side rebuilds the level from the dump and runs its own (already bit-exact) `BoxWorld`/`MeshWorld`. So the controller can be ported piece by piece, and the first frame where C++ and Rust disagree points at what's missing. Replaying world queries instead would need the whole controller ported before any trace could run.
- **Locomotion** is ported in full with code below.
- **Tuning** is ported field by field from the Rust source under the conventions below, and checked against a field dump Rust writes.

**Tech Stack:** C++20 / MSVC `/fp:strict`, doctest 2.5.3, Rust (cargo in `%USERPROFILE%\.cargo\bin`), glam 0.32.1, Git Bash, faith-runner `9470d755`.

**Spec:** `docs/superpowers/specs/2026-10-09-faith-movement-port-design.md` (M1, second part; spec §4.2 says the M1 plan decides how traces are produced. Decided: option A, a local never-committed patch.)

## Global Constraints

Everything in M1a's constraints still applies:
- C++20, no Unreal, standard library only; metres, Y up; `float` throughout.
- No `std::min`/`std::max` on floats: use `FMin`/`FMax`/`FClamp`.
- Golden comparisons are **bit-exact**.
- Sources are plain ASCII (a test enforces this).
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

New for M1b:
- **Every float literal in ported code carries `f`.** Rust literals in `f32` context are `f32`; an unsuffixed C++ literal is `double` and changes results.
- **The faith-runner reference clone is never modified.** The trace patch only ever lives in `../faith-runner-trace` (a worktree) and in this repo as a `.patch` file.
- **Traces are regenerated only by `tools/trace/make-traces.sh`.** It records the faith-runner SHA and the patch's hash in `RunlineCore/tests/golden/traces/TRACE_REFERENCE`.
- **Porting convention for `tuning.rs` (Task 4):** it's about 520 lines of field declarations and defaults, so the plan gives the rules and the check instead of re-typing it. Port `tuning.rs:11-357` (fields) and `:362-527` (defaults) in source order:
  - snake_case field names become camelCase members, with the Rust doc comments kept;
  - each default becomes a member initializer with the same expression;
  - `uu(x)` becomes `Uu(x)`;
  - every literal gets `f`.

  The golden field dump (Task 4, Step 5) checks every value bit for bit, and fails on any missing or extra field.

## Review Focus

1. **The reference clone stays clean after trace generation.** `git -C ../faith-runner status --porcelain` must be empty afterwards; otherwise every golden's REFERENCE turns `(dirty)`. Pinned in Task 1, Step 7.
2. **A test that builds several controllers, or none,** must not mix traces: each controller gets its own numbered file per test thread. Pinned in Task 1 (the multi-controller test `tests__drop_pressing_crouch_at` style is in the selection) and Task 2 (the loader requires one `init` per file).
3. **Empty event lists and fixture text containing commas** must parse correctly. Pinned in Task 2's parser unit test.
4. **Locomotion's braking loop with `dt = 0`** must not divide by zero, and must return what Rust returns. Pinned in Task 3's golden (dt = 0 rows).
5. **Tuning values computed from expressions** (for example `uu(1600.0 * 1600.0 / (2.0 * 780.0))`) must round exactly as Rust's do: float literals, same grouping. Pinned in Task 4's bit-exact field dump.

---

## File Structure

```
tools/trace/faith-runner-trace.patch   generated from ../faith-runner-trace (Task 1)
tools/trace/make-traces.sh             recreate worktree, apply patch, run tests, copy selection
tools/trace/selected.txt               which recorded traces to commit
RunlineCore/tests/golden/traces/*.trace, TRACE_REFERENCE                       (generated)
RunlineCore/tests/Trace.h              trace loader                             (Task 2)
RunlineCore/tests/trace_tests.cpp                                               (Task 2)
RunlineCore/include/runline/Locomotion.h, src/Locomotion.cpp                    (Task 3)
RunlineCore/tests/locomotion_tests.cpp                                          (Task 3)
tools/golden/src/locomotion.rs         curve + acceleration goldens             (Task 3)
RunlineCore/include/runline/Tuning.h, src/Tuning.cpp                            (Task 4)
RunlineCore/tests/tuning_tests.cpp                                              (Task 4)
tools/golden/src/tuning.rs             Tuning::default() field dump             (Task 4)
```

---

### Task 1: Record faith-runner's tests as traces (option A)

**Files:**
- Create: `tools/trace/faith-runner-trace.patch` (generated), `tools/trace/make-traces.sh`, `tools/trace/selected.txt`, `RunlineCore/tests/golden/traces/*.trace` and `TRACE_REFERENCE` (generated)
- Modify: `.gitattributes`, `CLAUDE.md`
- Outside this repo (never committed anywhere): the worktree `C:\school\Personal Project\faith-runner-trace`

**Interfaces:**
- Produces the trace format that Task 2 reads. One line per record, comma-separated, first field the kind:
  - `trace,1`: the format version, which is the first line.
  - `tuning_default,0|1`: whether the controller's tuning equals `Tuning::default()`.
  - `init,fx,fy,fz,vx,vy,vz,yaw,pitch,state,crouched,sprint_charge`: the state before the first step.
  - `world` … `end_world`: the geometry, written before the first frame and again whenever it changes. Inside it:
    - `box,minx,miny,minz,maxx,maxy,maxz`
    - `tri,ax,ay,az,bx,by,bz,cx,cy,cz`
    - `fixture,<Rust Debug text, may contain commas>`
  - `in,dt,mx,my,lx,ly,jump_pressed,jump_held,crouch_pressed,crouch_held,turn_pressed,melee_pressed,takedown_pressed,strafe_raw`: one frame's input.
  - `out,fx,fy,fz,vx,vy,vz,yaw,pitch,state,crouched,sprint_charge,events`: the state after that step. `events` are variant names joined by `;`, and may be empty.
- Bools are written as `0` or `1`; floats in Rust's shortest round-trip form; `state` is `State::name()`.
- File names are `<test path with :: replaced by __>__<n>.trace`, where `n` counts controllers on that test's thread from 1.

- [ ] **Step 1: Create the trace worktree at the reference commit**

```bash
git -C "C:/school/Personal Project/faith-runner" worktree add --detach "C:/school/Personal Project/faith-runner-trace" 9470d755b6aa4e4fa9257fc1326abcfc3ad4b028
```

Expected: `Preparing worktree (detached HEAD 9470d75)`. `git -C "C:/school/Personal Project/faith-runner" status --porcelain` prints nothing.

- [ ] **Step 2: Add `World::dump` in the worktree's `crates/faith_move/src/world.rs`**

In `pub trait World`, after `fn fixtures(...)`, add:

```rust
    /// (Runline trace patch) The geometry as text, for RunlineCore's replay tests.
    fn dump(&self) -> String {
        String::new()
    }
```

In `impl World for BoxWorld`, add:

```rust
    fn dump(&self) -> String {
        let mut s = String::new();
        for b in &self.boxes {
            s += &format!("box,{},{},{},{},{},{}\n", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
        }
        for fx in &self.fixtures {
            s += &format!("fixture,{:?}\n", fx);
        }
        s
    }
```

In `impl World for MeshWorld`, add:

```rust
    fn dump(&self) -> String {
        let mut s = String::new();
        for t in &self.tris {
            s += &format!("tri,{},{},{},{},{},{},{},{},{}\n", t[0].x, t[0].y, t[0].z, t[1].x, t[1].y, t[1].z, t[2].x, t[2].y, t[2].z);
        }
        for fx in &self.fixtures {
            s += &format!("fixture,{:?}\n", fx);
        }
        s
    }
```

In `impl World for Layered<'_>`, add:

```rust
    fn dump(&self) -> String {
        self.still.dump() + &self.moving.dump()
    }
```

- [ ] **Step 3: Add the recorder, `crates/faith_move/src/trace.rs`, in the worktree**

```rust
//! (Runline trace patch) Records every Controller::step a test makes, for RunlineCore's replay
//! tests. Only active when FAITH_TRACE_DIR is set. Never committed to faith-runner.
use crate::controller::{Controller, Input};
use crate::tuning::Tuning;
use crate::world::World;
use std::cell::RefCell;
use std::io::Write as _;

thread_local! {
    /// Controllers seen on this test thread (numbered from 1), with the world each last wrote.
    static SEEN: RefCell<Vec<String>> = RefCell::new(Vec::new());
}

fn dir() -> Option<String> {
    std::env::var("FAITH_TRACE_DIR").ok()
}

fn append(id: usize, text: &str) {
    let name = std::thread::current().name().unwrap_or("main").replace("::", "__");
    let path = format!("{}/{}__{}.trace", dir().unwrap(), name, id);
    let mut f = std::fs::OpenOptions::new().create(true).append(true).open(path).unwrap();
    f.write_all(text.as_bytes()).unwrap();
}

fn b(x: bool) -> u8 {
    x as u8
}

fn snapshot(c: &Controller) -> String {
    format!(
        "{},{},{},{},{},{},{},{},{},{},{}",
        c.feet.x, c.feet.y, c.feet.z, c.vel.x, c.vel.y, c.vel.z, c.yaw, c.pitch, c.state.name(), b(c.crouched), c.sprint_charge
    )
}

pub fn before(c: &mut Controller, dt: f32, i: &Input, w: &dyn World) {
    if dir().is_none() {
        return;
    }
    let mut text = String::new();
    if c.trace_id == 0 {
        c.trace_id = SEEN.with(|s| {
            let mut s = s.borrow_mut();
            s.push(String::new());
            s.len()
        });
        let default = format!("{:?}", c.tuning) == format!("{:?}", Tuning::default());
        text += &format!("trace,1\ntuning_default,{}\ninit,{}\n", b(default), snapshot(c));
    }
    let dump = w.dump();
    let changed = SEEN.with(|s| {
        let mut s = s.borrow_mut();
        let last = &mut s[c.trace_id - 1];
        if *last != dump || text.starts_with("trace,") {
            *last = dump.clone();
            true
        } else {
            false
        }
    });
    if changed {
        text += &format!("world\n{}end_world\n", dump);
    }
    text += &format!(
        "in,{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
        dt, i.move_axis.x, i.move_axis.y, i.look.x, i.look.y,
        b(i.jump_pressed), b(i.jump_held), b(i.crouch_pressed), b(i.crouch_held),
        b(i.turn_pressed), b(i.melee_pressed), b(i.takedown_pressed), i.strafe_raw
    );
    append(c.trace_id, &text);
}

pub fn after(c: &Controller) {
    if dir().is_none() || c.trace_id == 0 {
        return;
    }
    let events: Vec<String> = c
        .events
        .iter()
        .map(|e| format!("{:?}", e).split(|ch: char| !ch.is_alphanumeric() && ch != '_').next().unwrap_or("").to_string())
        .collect();
    append(c.trace_id, &format!("out,{},{}\n", snapshot(c), events.join(";")));
}
```

In the worktree's `crates/faith_move/src/lib.rs`, after `#[cfg(test)] mod tests;`, add:

```rust
#[cfg(test)]
mod trace;
```

- [ ] **Step 4: Wrap `Controller::step` and add the trace id, in the worktree's `controller.rs`**

In `pub struct Controller`, after `pub(crate) hang_time: f32,`, add:

```rust
    /// (Runline trace patch) Which trace file this controller writes; 0 until its first step.
    #[cfg(test)]
    pub(crate) trace_id: usize,
```

In `Controller::new`, after `hang_time: 0.0,`, add:

```rust
            #[cfg(test)]
            trace_id: 0,
```

Rename the existing `pub fn step(&mut self, dt: f32, input: &Input, world: &dyn World) {` to `fn step_inner(&mut self, dt: f32, input: &Input, world: &dyn World) {`, keeping its body and doc comment as they are. Directly above it, add:

```rust
    /// Advance by `dt` seconds.
    pub fn step(&mut self, dt: f32, input: &Input, world: &dyn World) {
        #[cfg(test)]
        crate::trace::before(self, dt, input, world);
        self.step_inner(dt, input, world);
        #[cfg(test)]
        crate::trace::after(self);
    }
```

- [ ] **Step 5: Check the patch changes nothing when tracing is off, then save it**

Run in `C:\school\Personal Project\faith-runner-trace`: `cargo test -q -p faith_move --release`
Expected: `146 passed; 0 failed; 2 ignored`, the same as unpatched, with no warnings from `trace.rs`.

Then, from this repo:

```bash
mkdir -p tools/trace
git -C "C:/school/Personal Project/faith-runner-trace" add -A
git -C "C:/school/Personal Project/faith-runner-trace" diff --cached > tools/trace/faith-runner-trace.patch
```

Expected: the patch touches `world.rs`, `controller.rs` and `lib.rs`, and creates `trace.rs`.

- [ ] **Step 6: Write `tools/trace/make-traces.sh` and `tools/trace/selected.txt`**

```bash
#!/usr/bin/env bash
# Records faith-runner's own tests as per-frame traces for RunlineCore's replay tests.
# Works in a separate worktree of the reference clone (../faith-runner-trace) with the trace
# patch applied, so the reference clone itself stays clean.
# Usage (Git Bash, from anywhere): bash tools/trace/make-traces.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
ref="$root/../faith-runner"
wt="$root/../faith-runner-trace"
sha="$(sed -n 's/^faith-runner \([0-9a-f]\{40\}\).*/\1/p' "$root/RunlineCore/tests/golden/REFERENCE")"
patch="$root/tools/trace/faith-runner-trace.patch"
[ -n "$sha" ] || { echo "can't read the faith-runner SHA from REFERENCE" >&2; exit 1; }
[ -d "$wt" ] || git -C "$ref" worktree add --detach "$wt" "$sha"
git -C "$wt" checkout -q --detach "$sha"
git -C "$wt" reset -q --hard
git -C "$wt" clean -fdq
git -C "$wt" apply "$patch"
out="$(mktemp -d)"
( cd "$wt" && FAITH_TRACE_DIR="$(cygpath -w "$out")" PATH="$HOME/.cargo/bin:$PATH" cargo test -q -p faith_move --release )
dest="$root/RunlineCore/tests/golden/traces"
mkdir -p "$dest"
rm -f "$dest"/*.trace
while read -r name; do
  { [ -z "$name" ] || [ "${name#\#}" != "$name" ]; } && continue
  [ -f "$out/$name.trace" ] || { echo "no trace recorded for $name" >&2; exit 1; }
  cp "$out/$name.trace" "$dest/"
done < "$root/tools/trace/selected.txt"
printf 'faith-runner %s + trace patch %s\n' "$sha" "$(sha256sum "$patch" | cut -c1-16)" > "$dest/TRACE_REFERENCE"
echo "recorded $(ls "$out" | wc -l) traces, kept $(ls "$dest"/*.trace | wc -l)"
[ -z "$(git -C "$ref" status --porcelain)" ] || { echo "reference clone is dirty" >&2; exit 1; }
```

`tools/trace/selected.txt` lists the controller core M1c starts with: falling, running, sprinting, jumping and landing.

```
# One recorded trace per line (file name without .trace). Lines starting with # are ignored.
tests__falls_and_lands__1
tests__sprint_builds_up__1
tests__jump_arc_and_land__1
tests__jump_landing_hands_back_the_boost__1
tests__hard_landing_stops_dead__1
```

Append to `.gitattributes`:

```
*.trace text eol=lf
```

- [ ] **Step 7: Generate the traces, and check the reference clone stayed clean**

Run: `bash tools/trace/make-traces.sh`
Expected:
- It ends with `recorded N traces, kept 5`, where N is over 100 (every test's controllers).
- No `reference clone is dirty` message (Review Focus 1).
- `RunlineCore/tests/golden/traces/` holds five `.trace` files and `TRACE_REFERENCE`.
- `head -4 RunlineCore/tests/golden/traces/tests__falls_and_lands__1.trace` shows `trace,1`, `tuning_default,1`, an `init,…` line, and `world`.

If a selected name doesn't exist (for example the test runs its controller on another thread), list the recorded files, pick the matching name, and record a `Ruling:` in the ledger.

Then also check: `ls <the temp dir printed>` should contain several `__2.trace` files from tests that build more than one controller (Review Focus 2). If there are none, record that in the ledger. Task 2's loader still enforces one `init` per file.

- [ ] **Step 8: Document it, and commit**

In `CLAUDE.md`, under `## Build and test`, add:

```markdown
- Controller traces: `bash tools/trace/make-traces.sh` (Git Bash) applies tools/trace/faith-runner-trace.patch
  to a separate worktree ../faith-runner-trace (the reference clone stays clean), runs faith_move's
  tests with FAITH_TRACE_DIR set, and copies the traces named in tools/trace/selected.txt into
  RunlineCore/tests/golden/traces/. Commit them with TRACE_REFERENCE.
```

```bash
git add .gitattributes CLAUDE.md tools/trace RunlineCore/tests/golden/traces
git commit -m "M1b: record faith-runner's tests as per-frame traces (local patch, separate worktree)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The trace loader

**Files:**
- Create: `RunlineCore/tests/Trace.h`, `RunlineCore/tests/trace_tests.cpp`
- Modify: `RunlineCore/CMakeLists.txt` (add `tests/trace_tests.cpp`)

**Interfaces:**
- Consumes: the trace format (Task 1); `runline::Aabb`, `Tri`, `BoxWorld`, `MeshWorld`.
- Produces, in namespace `trace`:
  - `struct Snapshot { runline::Vec3 feet, vel; float yaw, pitch; std::string state; bool crouched; float sprintCharge; }`
  - `struct FrameInput { float dt, moveX, moveY, lookX, lookY; bool jumpPressed, jumpHeld, crouchPressed, crouchHeld, turnPressed, meleePressed, takedownPressed; float strafeRaw; }`
  - `struct WorldDump { std::vector<runline::Aabb> boxes; std::vector<runline::Tri> tris; std::vector<std::string> fixtures; }`
  - `struct Frame { size_t world; FrameInput in; Snapshot out; std::vector<std::string> events; }`
  - `struct Trace { std::string name; bool defaultTuning; Snapshot init; std::vector<WorldDump> worlds; std::vector<Frame> frames; }`
  - `Trace Parse(std::istream&, const std::string& name)`, `Trace Load(const std::string& name)` and `std::vector<std::string> All()` (the names of every committed trace)
  - `runline::BoxWorld Boxes(const WorldDump&)` and `runline::MeshWorld Mesh(const WorldDump&)`

- [ ] **Step 1: Write the failing tests, `RunlineCore/tests/trace_tests.cpp`**

```cpp
#include "Trace.h"
#include <sstream>
using namespace runline;

TEST_CASE("Parse reads every record kind, empty events and fixture text with commas") {   // Review Focus 3
    std::istringstream in(
        "trace,1\n"
        "tuning_default,1\n"
        "init,0,1,0,0,0,0,0,0,Air,0,0\n"
        "world\n"
        "box,-50,-1,-50,50,0,50\n"
        "tri,0,0,0,1,0,0,0,0,1\n"
        "fixture,ZipLine { a: Vec3(0.0, 5.0, 0.0), b: Vec3(9.0, 1.0, 0.0) }\n"
        "end_world\n"
        "in,0.016666668,0,1,0.1,-0.2,1,1,0,0,0,0,0,0\n"
        "out,0,0.9,0,0,-0.27,0,0.1,-0.2,Air,0,0,\n"
        "in,0.016666668,0,0,0,0,0,0,0,0,0,0,0,0\n"
        "out,0,0,0,0,0,0,0.1,-0.2,Ground,0,0,Land;Jump\n");
    const trace::Trace t = trace::Parse(in, "mini");
    CHECK(t.defaultTuning);
    CHECK(t.init.state == "Air");
    CHECK(t.init.feet == Vec3{0, 1, 0});
    REQUIRE(t.worlds.size() == 1);
    CHECK(t.worlds[0].boxes.size() == 1);
    CHECK(t.worlds[0].tris.size() == 1);
    REQUIRE(t.worlds[0].fixtures.size() == 1);
    CHECK(t.worlds[0].fixtures[0] == "ZipLine { a: Vec3(0.0, 5.0, 0.0), b: Vec3(9.0, 1.0, 0.0) }");
    REQUIRE(t.frames.size() == 2);
    CHECK(t.frames[0].world == 0);
    CHECK(t.frames[0].in.moveY == 1.0f);
    CHECK(t.frames[0].in.jumpPressed);
    CHECK_FALSE(t.frames[0].in.crouchHeld);
    CHECK(t.frames[0].out.vel.y == doctest::Approx(-0.27f));
    CHECK(t.frames[0].events.empty());
    CHECK(t.frames[1].out.state == "Ground");
    CHECK(t.frames[1].events == std::vector<std::string>{"Land", "Jump"});
}

TEST_CASE("Parse rejects a file with no init or a frame with no output") {
    std::istringstream noInit("trace,1\ntuning_default,1\nin,0.1,0,0,0,0,0,0,0,0,0,0,0,0\n");
    CHECK_THROWS(trace::Parse(noInit, "bad"));
    std::istringstream noOut("trace,1\ntuning_default,1\ninit,0,0,0,0,0,0,0,0,Air,0,0\nworld\nend_world\nin,0.1,0,0,0,0,0,0,0,0,0,0,0,0\n");
    CHECK_THROWS(trace::Parse(noOut, "bad"));
}

TEST_CASE("every committed trace loads, and was recorded from the golden reference commit") {
    const auto names = trace::All();
    REQUIRE(names.size() >= 5);
    for (const auto& n : names) {
        CAPTURE(n);
        const trace::Trace t = trace::Load(n);
        CHECK(t.frames.size() >= 10);
        CHECK_FALSE(t.worlds.empty());
        CHECK_FALSE(t.init.state.empty());
    }
    std::ifstream ref(std::string(RUNLINE_GOLDEN_DIR) + "/REFERENCE"), tref(std::string(RUNLINE_GOLDEN_DIR) + "/traces/TRACE_REFERENCE");
    std::string a, b;
    std::getline(ref, a);
    std::getline(tref, b);
    REQUIRE(a.size() >= 53);
    CHECK(b.rfind(a.substr(0, 53), 0) == 0);   // "faith-runner <40-hex sha>" is the same commit
}

TEST_CASE("falls_and_lands: the floor rebuilds, and the trace ends standing on it") {
    const trace::Trace t = trace::Load("tests__falls_and_lands__1");
    const BoxWorld w = trace::Boxes(t.worlds[t.frames.back().world]);
    CHECK(w.Overlaps(Aabb::New({-1, -0.5f, -1}, {1, -0.1f, 1})));   // the floor is there
    CHECK(t.frames.back().out.state == "Ground");
    CHECK(std::fabs(t.frames.back().out.feet.y) < 1e-3f);
}
```

Add `tests/trace_tests.cpp` to `add_executable(RunlineCoreTests ...)` in `RunlineCore/CMakeLists.txt`. Create a placeholder `RunlineCore/tests/Trace.h` containing `#pragma once`.

- [ ] **Step 2: Run them and check they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: build errors, `trace::Parse` undeclared.

- [ ] **Step 3: Write `RunlineCore/tests/Trace.h`**

```cpp
#pragma once
// Reads the per-frame traces tools/trace/make-traces.sh records from faith-runner's own tests:
// the geometry, the state before the first step, and each frame's input and resulting state.
#include <doctest/doctest.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>
#include "runline/BoxWorld.h"
#include "runline/MeshWorld.h"

namespace trace {

struct Snapshot {
    runline::Vec3 feet{}, vel{};
    float yaw = 0, pitch = 0;
    std::string state;
    bool crouched = false;
    float sprintCharge = 0;
};

struct FrameInput {
    float dt = 0, moveX = 0, moveY = 0, lookX = 0, lookY = 0;
    bool jumpPressed = false, jumpHeld = false, crouchPressed = false, crouchHeld = false;
    bool turnPressed = false, meleePressed = false, takedownPressed = false;
    float strafeRaw = 0;
};

struct WorldDump {
    std::vector<runline::Aabb> boxes;
    std::vector<runline::Tri> tris;
    std::vector<std::string> fixtures;   // Rust Debug text; parsed when fixtures are ported (M4)
};

struct Frame {
    size_t world = 0;   // index into Trace::worlds
    FrameInput in;
    Snapshot out;
    std::vector<std::string> events;
    bool hasOut = false;
};

struct Trace {
    std::string name;
    bool defaultTuning = false;
    bool hasInit = false;
    Snapshot init;
    std::vector<WorldDump> worlds;
    std::vector<Frame> frames;
};

/// Split on `sep`, keeping empty fields (a trailing separator gives a trailing "").
inline std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out(1);
    for (char c : s) {
        if (c == sep) out.emplace_back();
        else out.back() += c;
    }
    return out;
}

inline Snapshot ParseSnapshot(const std::vector<std::string>& f, size_t at) {
    Snapshot s;
    s.feet = {std::stof(f[at]), std::stof(f[at + 1]), std::stof(f[at + 2])};
    s.vel = {std::stof(f[at + 3]), std::stof(f[at + 4]), std::stof(f[at + 5])};
    s.yaw = std::stof(f[at + 6]);
    s.pitch = std::stof(f[at + 7]);
    s.state = f[at + 8];
    s.crouched = f[at + 9] == "1";
    s.sprintCharge = std::stof(f[at + 10]);
    return s;
}

inline Trace Parse(std::istream& in, const std::string& name) {
    Trace t;
    t.name = name;
    std::string line;
    bool inWorld = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (inWorld) {
            if (line == "end_world") { inWorld = false; continue; }
            if (line.rfind("fixture,", 0) == 0) { t.worlds.back().fixtures.push_back(line.substr(8)); continue; }
            const auto f = Split(line, ',');
            if (f[0] == "box") {
                t.worlds.back().boxes.push_back(runline::Aabb::New({std::stof(f[1]), std::stof(f[2]), std::stof(f[3])}, {std::stof(f[4]), std::stof(f[5]), std::stof(f[6])}));
            } else if (f[0] == "tri") {
                runline::Tri tri;
                for (int k = 0; k < 3; ++k) tri[k] = {std::stof(f[1 + 3 * k]), std::stof(f[2 + 3 * k]), std::stof(f[3 + 3 * k])};
                t.worlds.back().tris.push_back(tri);
            } else {
                throw std::runtime_error(name + ": unknown world record " + f[0]);
            }
            continue;
        }
        const auto f = Split(line, ',');
        if (f[0] == "trace") {
            if (f.size() < 2 || f[1] != "1") throw std::runtime_error(name + ": unsupported trace version");
        } else if (f[0] == "tuning_default") {
            t.defaultTuning = f[1] == "1";
        } else if (f[0] == "init") {
            if (t.hasInit) throw std::runtime_error(name + ": two controllers in one trace");
            t.init = ParseSnapshot(f, 1);
            t.hasInit = true;
        } else if (f[0] == "world") {
            t.worlds.emplace_back();
            inWorld = true;
        } else if (f[0] == "in") {
            if (!t.hasInit || t.worlds.empty()) throw std::runtime_error(name + ": input before init/world");
            if (!t.frames.empty() && !t.frames.back().hasOut) throw std::runtime_error(name + ": frame with no output");
            Frame fr;
            fr.world = t.worlds.size() - 1;
            FrameInput& i = fr.in;
            i.dt = std::stof(f[1]);
            i.moveX = std::stof(f[2]);
            i.moveY = std::stof(f[3]);
            i.lookX = std::stof(f[4]);
            i.lookY = std::stof(f[5]);
            i.jumpPressed = f[6] == "1";
            i.jumpHeld = f[7] == "1";
            i.crouchPressed = f[8] == "1";
            i.crouchHeld = f[9] == "1";
            i.turnPressed = f[10] == "1";
            i.meleePressed = f[11] == "1";
            i.takedownPressed = f[12] == "1";
            i.strafeRaw = std::stof(f[13]);
            t.frames.push_back(fr);
        } else if (f[0] == "out") {
            if (t.frames.empty() || t.frames.back().hasOut) throw std::runtime_error(name + ": output with no input");
            Frame& fr = t.frames.back();
            fr.out = ParseSnapshot(f, 1);
            if (f.size() > 12 && !f[12].empty()) fr.events = Split(f[12], ';');
            fr.hasOut = true;
        } else {
            throw std::runtime_error(name + ": unknown record " + f[0]);
        }
    }
    if (!t.hasInit) throw std::runtime_error(name + ": no init record");
    if (!t.frames.empty() && !t.frames.back().hasOut) throw std::runtime_error(name + ": frame with no output");
    return t;
}

inline std::string Dir() { return std::string(RUNLINE_GOLDEN_DIR) + "/traces"; }

inline Trace Load(const std::string& name) {
    std::ifstream in(Dir() + "/" + name + ".trace");
    REQUIRE_MESSAGE(in.good(), name << ".trace missing; run bash tools/trace/make-traces.sh");
    return Parse(in, name);
}

/// The names of every committed trace, sorted.
inline std::vector<std::string> All() {
    std::vector<std::string> names;
    for (const auto& e : std::filesystem::directory_iterator(Dir()))
        if (e.path().extension() == ".trace") names.push_back(e.path().stem().string());
    std::sort(names.begin(), names.end());
    return names;
}

inline runline::BoxWorld Boxes(const WorldDump& d) {
    runline::BoxWorld w;
    for (const auto& b : d.boxes) w.Add(b);
    return w;
}

inline runline::MeshWorld Mesh(const WorldDump& d) {
    runline::MeshWorld w;
    w.tris = d.tris;
    return w;
}

} // namespace trace
```

Add `#include <algorithm>` with the other includes (for `std::sort`).

- [ ] **Step 4: Run and check it passes**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 53 test cases (49 + 4).

Mutation check (revert afterwards): in `Split`, drop empty fields by changing `else out.back() += c;` to `else if (c != ' ') out.back() += c;` and the separator branch to `if (c == sep && !out.back().empty()) out.emplace_back();`. Expected: the parser test FAILS (empty events, or columns shift). Revert, re-run: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add RunlineCore
git commit -m "M1b: trace loader for faith-runner's recorded tests

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Locomotion (the sprint curve, ground acceleration and braking)

**Files:**
- Create: `RunlineCore/include/runline/Locomotion.h`, `RunlineCore/src/Locomotion.cpp`, `RunlineCore/tests/locomotion_tests.cpp`, `tools/golden/src/locomotion.rs`, `RunlineCore/tests/golden/{curves,loco_accel,loco_velocity}.csv` (generated)
- Modify: `RunlineCore/CMakeLists.txt`, `tools/golden/src/main.rs`, `RunlineCore/tests/golden_tests.cpp`

**Interfaces:**
- Consumes: `Vec3`, `FMin`/`FMax`/`FClamp`, and the golden helpers.
- Produces:
  - `struct Curve { std::vector<std::pair<float, float>> points; float Eval(float x) const; }`
  - `SPEED_CURVE` (5 points)
  - `Curve AccelCurveFrom(const std::vector<std::pair<float, float>>& speed, std::size_t steps)`
  - `struct RunState { float energy, accelTime, stopping; }`
  - `struct RunInput { Vec3 forward, right; float aForward, aStrafe, turn; }`
  - `struct Locomotion`, with the fields of `locomotion.rs:86-112` (camelCase, same defaults) and `SprintAccel`, `WalkAccel`, `ControllerAccel` and `CalcVelocity`

- [ ] **Step 1: Write the failing tests, `RunlineCore/tests/locomotion_tests.cpp`**

These are faith-runner's own locomotion tests (`locomotion.rs:302-372`), ported.

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "runline/Locomotion.h"
using namespace runline;

namespace {
std::vector<std::pair<float, float>> Run(const Locomotion& loco, float secs, float dt) {
    RunState st;
    Vec3 vel{};
    const RunInput i{{0, 0, -1}, {1, 0, 0}, 1.0f, 0.0f, 0.0f};
    float t = 0.0f;
    std::vector<std::pair<float, float>> out;
    while (t < secs) {
        const Vec3 a = loco.ControllerAccel(st, i, vel, dt);
        vel = loco.CalcVelocity(vel, a, dt, 1.0f, loco.groundFriction);
        t += dt;
        out.push_back({t, vel.Length()});
    }
    return out;
}
} // namespace

TEST_CASE("the accel curve is the speed curve's slope") {
    const Curve c = AccelCurveFrom(SPEED_CURVE, 10);
    CHECK(c.points.size() == 11);
    CHECK(std::fabs(c.Eval(2.0f) - 10.0f) < 0.1f);
    CHECK(std::fabs(c.Eval(5.76f) - 0.52f) < 0.05f);
    CHECK(std::fabs(c.Eval(7.2f)) < 1e-3f);
}

TEST_CASE("sprinting straight from a standstill follows SpeedCurve_LightWeapon") {
    const Locomotion loco;
    const auto log = Run(loco, 8.0f, 1.0f / 60.0f);
    const auto at = [&](float t) { for (const auto& [x, v] : log) if (x >= t) return v; return log.back().second; };
    for (const auto& [t, v] : std::vector<std::pair<float, float>>{{0.4f, 4.0f}, {1.0f, 5.2f}, {3.5f, 6.5f}, {7.0f, 7.2f}}) {
        CAPTURE(t);
        CHECK(std::fabs(at(t) - v) < 0.25f);
    }
    CHECK(at(8.0f) <= 7.2f + 1e-3f);
}

TEST_CASE("letting go brakes to a stop in well under a second") {
    const Locomotion loco;
    RunState st{0.0f, 2.0f, 0.0f};
    Vec3 vel{0, 0, -6};
    const RunInput i{{0, 0, -1}, {1, 0, 0}, 0.0f, 0.0f, 0.0f};
    float t = 0.0f;
    while (vel.Length() > 0.0f && t < 2.0f) {
        const Vec3 a = loco.ControllerAccel(st, i, vel, 1.0f / 60.0f);
        vel = loco.CalcVelocity(vel, a, 1.0f / 60.0f, 1.0f, loco.groundFriction);
        t += 1.0f / 60.0f;
    }
    CHECK(t < 0.5f);
}

TEST_CASE("whipping the view round while sprinting costs speed") {
    const Locomotion loco;
    RunState st;
    const Vec3 vel{0, 0, -7};
    const RunInput straight{{0, 0, -1}, {1, 0, 0}, 1.0f, 0.0f, 0.0f};
    RunInput turning = straight;
    turning.turn = 0.2f;
    const Vec3 a0 = loco.SprintAccel(st, straight, vel, 1.0f / 60.0f, false);
    const Vec3 a1 = loco.SprintAccel(st, turning, vel, 1.0f / 60.0f, false);
    CHECK(a1.Dot({0, 0, -1}) < a0.Dot({0, 0, -1}) - 1.0f);
}
```

Add `src/Locomotion.cpp` to the library and `tests/locomotion_tests.cpp` to the tests in `RunlineCore/CMakeLists.txt`. Create a placeholder `RunlineCore/src/Locomotion.cpp` containing `#include "runline/Locomotion.h"`, and a placeholder `RunlineCore/include/runline/Locomotion.h` containing `#pragma once`.

- [ ] **Step 2: Run them and check they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: build errors, `Locomotion` undeclared.

- [ ] **Step 3: Write `RunlineCore/include/runline/Locomotion.h`**

```cpp
#pragma once
// Running on the ground, the way Mirror's Edge does it. Port of faith_move::locomotion, which
// ports the game's native ATdPawn::GetSprintAcceleration / GetWalkAcceleration / CalcVelocity.
// Units are metres and seconds.
#include <cstddef>
#include <utility>
#include <vector>
#include "runline/Vec3.h"

namespace runline {

/// TdPawn.SpeedCurve_LightWeapon: (seconds of running, speed m/s), linear.
inline const std::vector<std::pair<float, float>> SPEED_CURVE{{0.0f, 0.0f}, {0.4f, 4.0f}, {1.0f, 5.2f}, {3.5f, 6.5f}, {7.0f, 7.2f}};

/// A piecewise-linear FInterpCurveFloat (CIM_Linear points), evaluated the way
/// FInterpCurve::Eval does: clamped at both ends.
struct Curve {
    std::vector<std::pair<float, float>> points;
    float Eval(float x) const;
};

/// ATdPawn building AccelCurve_LightWeapon from the speed curve: acceleration against speed.
Curve AccelCurveFrom(const std::vector<std::pair<float, float>>& speed, std::size_t steps);

/// What the pawn carries between frames.
struct RunState {
    /// TdPawn.SpeedSprintEnergy (m/s above SpeedMaxBaseVelocity).
    float energy = 0.0f;
    /// TdPlayerController.AccelerationTime / bIsStopping (the tap-stop).
    float accelTime = 0.0f;
    float stopping = 0.0f;
};

/// One frame's input, as the controller hands it to the pawn.
struct RunInput {
    /// Facing (horizontal, unit) and its right.
    Vec3 forward{};
    Vec3 right{};
    /// PlayerInput.aForward / aStrafe (-1..1).
    float aForward = 0.0f;
    float aStrafe = 0.0f;
    /// How far the view turned this frame, in radians.
    float turn = 0.0f;
};

/// TdPawn's ground-movement numbers (defaults from TdGame.u, uu converted to m).
struct Locomotion {
    Curve accelCurve = AccelCurveFrom(SPEED_CURVE, 10);
    /// SpeedMaxBaseVelocity = 400: above this, the excess is sprint energy.
    float maxBase = 4.0f;
    /// SpeedMinBaseVelocity = 10.
    float minBase = 0.1f;
    /// Speed{Sprint,Walk,Strafe}VelocityAccelerationFactor = 30 / 7 / 10.
    float sprintFactor = 30.0f;
    float walkFactor = 7.0f;
    float strafeFactor = 10.0f;
    /// SpeedEnergyDecelerationTime = 3, SpeedEnergyDecelerationExponent = 0.5.
    float energyDecelTime = 3.0f;
    float energyDecelExponent = 0.5f;
    /// SpeedTurnDecelerationFactor = 10.
    float turnDecel = 10.0f;
    /// Pawn.GroundSpeed = 720 (top speed) and AccelRate = 6144 (most acceleration).
    float groundSpeed = 7.2f;
    float accelRate = 61.44f;
    /// PhysicsVolume.GroundFriction = 8.
    float groundFriction = 8.0f;
    /// TdPlayerPawn.BrakingFrictionStrength (TdPawn's own is 1.0).
    float brakingStrength = 0.5f;
    /// TdPlayerController.InputMaxSprintHeightLimit / RaduisLimit = 0.7.
    float sprintInputForward = 0.7f;
    float sprintInputSize = 0.7f;

    /// ATdPawn::GetSprintAcceleration.
    Vec3 SprintAccel(RunState& st, const RunInput& i, Vec3 vel, float dt, bool falling) const;
    /// ATdPawn::GetWalkAcceleration.
    Vec3 WalkAccel(RunState& st, const RunInput& i, Vec3 vel, float dt, bool falling) const;
    /// TdPlayerController's PlayerWalking: which acceleration to ask for, and the tap-stop.
    /// May set `vel` for the stop.
    Vec3 ControllerAccel(RunState& st, const RunInput& i, Vec3& vel, float dt) const;
    /// ATdPawn::CalcVelocity for walking.
    Vec3 CalcVelocity(Vec3 vel, Vec3 accel, float dt, float speedMod, float friction) const;

private:
    Vec3 InputDir(const RunInput& i) const;
    Vec3 FrictionComp(Vec3 vel) const;
    Vec3 TurnDamping(Vec3 vel, float turn) const;
};

} // namespace runline
```

- [ ] **Step 4: Write `RunlineCore/src/Locomotion.cpp`, a line-for-line port of `locomotion.rs:30-300`**

```cpp
#include "runline/Locomotion.h"
#include <cmath>

namespace runline {
namespace {

/// std::f32::consts::TAU.
constexpr float kTau = 6.28318530717958647692f;

Vec3 Horiz(Vec3 v) { return {v.x, 0.0f, v.z}; }

/// glam's abs().max_element().
float MaxAbs(Vec3 v) { return FMax(std::fabs(v.x), FMax(std::fabs(v.y), std::fabs(v.z))); }

/// The game rounds requested acceleration to 0.1 uu/s^2 (floor(x * 10 + 0.5) / 10).
Vec3 RoundAccel(Vec3 a) {
    const auto r = [](float x) { return (std::floor(x * 100.0f * 10.0f + 0.5f) / 10.0f) / 100.0f; };
    return {r(a.x), r(a.y), r(a.z)};
}

} // namespace

float Curve::Eval(float x) const {
    const auto& p = points;
    const std::size_t n = p.size();
    if (n == 0) return 0.0f;
    if (n == 1) return p[0].second;
    if (x <= p[0].first) return p[0].second;
    if (x >= p[n - 1].first) return p[n - 1].second;
    for (std::size_t k = 0; k + 1 < n; ++k) {
        const auto [x0, y0] = p[k];
        const auto [x1, y1] = p[k + 1];
        if (x < x1) {
            const float d = x1 - x0;
            return d <= 0.0f ? y0 : y0 + (y1 - y0) * (x - x0) / d;
        }
    }
    return p[n - 1].second;
}

Curve AccelCurveFrom(const std::vector<std::pair<float, float>>& speed, std::size_t steps) {
    const Curve s{speed};
    const auto [tLast, vLast] = speed.back();
    const std::size_t samples = speed.size() * 10;
    Curve inverse;
    for (std::size_t k = 0; k <= samples; ++k) {
        const float t = static_cast<float>(k) * tLast / static_cast<float>(samples);
        inverse.points.push_back({s.Eval(t), t});
    }
    const float dt = 0.5f / static_cast<float>(steps);
    Curve out;
    for (std::size_t i = 0; i <= steps; ++i) {
        const float v = static_cast<float>(i) * vLast / static_cast<float>(steps);
        const float t = inverse.Eval(v);
        out.points.push_back({v, (s.Eval(t + dt) - s.Eval(t)) / dt});
    }
    return out;
}

Vec3 Locomotion::InputDir(const RunInput& i) const { return (i.forward * i.aForward + i.right * i.aStrafe).NormalizeOrZero(); }

/// Friction compensation both accelerations add on the ground: CalcVelocity takes velocity x
/// friction x 0.1 off every frame, so this hands it back.
Vec3 Locomotion::FrictionComp(Vec3 vel) const { return vel * groundFriction * 0.1f; }

Vec3 Locomotion::TurnDamping(Vec3 vel, float turn) const {
    const float turnUu = std::fabs(turn) * 65536.0f / kTau;
    return vel * turnDecel * turnUu / 32768.0f;
}

Vec3 Locomotion::SprintAccel(RunState& st, const RunInput& i, Vec3 vel, float dt, bool falling) const {
    const float speed = Horiz(vel).Length();
    const Vec3 dir = InputDir(i);
    if (MaxAbs(dir) < 1e-4f) {
        st.energy = 0.0f;
        return Vec3::Zero();
    }
    st.energy = FMax(speed - maxBase, 0.0f);
    Vec3 accel = dir * accelCurve.Eval(speed);
    if (!falling) {
        // Steer the velocity onto the input direction, keeping its speed.
        const Vec3 diff{dir.x * speed - vel.x, 0.0f, dir.z * speed - vel.z};
        const float d = diff.Length();
        const float steer = FMin(sprintFactor * d, dt > 0.0f ? d / dt : 0.0f);
        accel += diff.NormalizeOrZero() * steer + FrictionComp(vel);
    }
    return RoundAccel(accel - TurnDamping(vel, i.turn));
}

Vec3 Locomotion::WalkAccel(RunState& st, const RunInput& i, Vec3 vel, float dt, bool falling) const {
    const Vec3 dir = InputDir(i);
    if (MaxAbs(dir) < 1e-4f) {
        st.energy = 0.0f;
        return Vec3::Zero();
    }
    // Sprint energy drains unless you keep pushing the way you face.
    if (st.energy > 0.0f) {
        const float along = FClamp(dir.Dot(i.forward), 0.0f, 0.9f);
        const float drain = (groundSpeed - maxBase) * (1.0f - std::pow(along, energyDecelExponent)) / energyDecelTime;
        st.energy = FMax(st.energy - drain * dt, 0.0f);
    }
    // Aim for MinBase along the input plus (MaxBase - MinBase + energy) scaled by the stick, per
    // axis, and close the gap at the walk (forward) and strafe rates.
    const float top = maxBase - minBase + st.energy;
    const float wantF = dir.Dot(i.forward) * minBase + top * i.aForward;
    const float wantS = dir.Dot(i.right) * minBase + top * i.aStrafe;
    const float haveF = vel.Dot(i.forward);
    const float haveS = vel.Dot(i.right);
    Vec3 accel = i.forward * (wantF - haveF) * walkFactor + i.right * (wantS - haveS) * strafeFactor;
    if (!falling) accel += FrictionComp(vel);
    return RoundAccel(accel);
}

Vec3 Locomotion::ControllerAccel(RunState& st, const RunInput& i, Vec3& vel, float dt) const {
    if (st.stopping > 0.0f) {
        st.stopping -= dt;
        vel = Horiz(vel).NormalizeOrZero() * 0.35f;
        return vel;
    }
    const Vec3 dir = InputDir(i);
    if (Horiz(dir).Length() > 0.0f) {
        st.accelTime += dt;
        const float size = std::sqrt(i.aForward * i.aForward + i.aStrafe * i.aStrafe);
        const bool sprint = i.aForward > sprintInputForward && size > sprintInputSize && vel.Dot(dir) > 0.0f;
        return sprint ? SprintAccel(st, i, vel, dt, false) : WalkAccel(st, i, vel, dt, false);
    }
    const Vec3 a = WalkAccel(st, i, vel, dt, false);
    if (st.accelTime > 0.0f && st.accelTime < 0.15f) {
        st.stopping = 0.25f;
        vel = Horiz(vel).NormalizeOrZero() * 0.35f;
    }
    st.accelTime = 0.0f;
    return a;
}

Vec3 Locomotion::CalcVelocity(Vec3 vel, Vec3 accel, float dt, float speedMod, float friction) const {
    const float maxAccel = accelRate * speedMod;
    const float maxSpeed = groundSpeed * speedMod;
    const Vec3 a = accel.LengthSquared() > maxAccel * maxAccel ? accel.Normalize() * maxAccel : accel;
    Vec3 v = vel;
    if (a != Vec3::Zero()) {
        v -= v * dt * friction * 0.1f;
    } else {
        // Braking, in steps of at most 0.03 s at 2 x friction x BrakingFrictionStrength; the result
        // is the average over the steps, and it stops dead once reversed or slow.
        const Vec3 before = v;
        float left = dt;
        Vec3 avg{};
        while (left > 0.0f) {
            const float step = FMin(left, 0.03f);
            left -= step;
            v -= v * 2.0f * step * friction * brakingStrength;
            if (v.Dot(before) > 0.0f && dt > 0.0f) avg += v * step / dt;
        }
        v = avg;
        if (v.Dot(before) < 0.0f || v.LengthSquared() < 0.1f * 0.1f) v = Vec3::Zero();
    }
    v += a * dt;
    if (v.LengthSquared() > maxSpeed * maxSpeed) v = v.Normalize() * maxSpeed;
    return v;
}

} // namespace runline
```

- [ ] **Step 5: Run and check it passes**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 57 test cases.

- [ ] **Step 6: Generate the locomotion goldens**

Create `tools/golden/src/locomotion.rs`:

```rust
//! locomotion.rs: the speed and acceleration curves, and the ground acceleration and velocity
//! update run on random states, including dt = 0 and the tap-stop.
use crate::common::{f, Lcg};
use faith_move::locomotion::{Curve, Locomotion, RunInput, RunState, SPEED_CURVE};
use glam::Vec3;

/// kind 0 = an accel-curve point (x, y); 1 = speed curve eval; 2 = accel curve eval.
pub fn curves_csv() -> String {
    let loco = Locomotion::default();
    let speed = Curve(SPEED_CURVE.to_vec());
    let mut s = String::from("kind,x,y\n");
    for (x, y) in &loco.accel_curve.0 {
        s += &format!("0,{},{}\n", x, y);
    }
    for i in 0..=200 {
        let x = -1.0 + i as f32 * 0.05;
        s += &format!("1,{},{}\n", x, speed.eval(x));
        s += &format!("2,{},{}\n", x, loco.accel_curve.eval(x));
    }
    s
}

fn pick(r: &mut Lcg) -> f32 {
    let x = r.next();
    if x < 0.25 { 0.0 } else if x < 0.5 { 1.0 } else { r.range(-1.0, 1.0) }
}

fn rand_input(r: &mut Lcg) -> RunInput {
    let a = r.range(0.0, std::f32::consts::TAU);
    RunInput {
        forward: Vec3::new(-a.sin(), 0.0, -a.cos()),
        right: Vec3::new(a.cos(), 0.0, -a.sin()),
        a_forward: pick(r),
        a_strafe: pick(r),
        turn: if r.next() < 0.5 { 0.0 } else { r.range(-0.3, 0.3) },
    }
}

fn rand_state(r: &mut Lcg) -> RunState {
    let energy = if r.next() < 0.5 { 0.0 } else { r.range(0.0, 3.2) };
    let accel_time = match (r.next() * 3.0) as u32 { 0 => 0.0, 1 => r.range(0.0, 0.2), _ => r.range(0.0, 3.0) };
    let stopping = if r.next() < 0.8 { 0.0 } else { r.range(0.0, 0.25) };
    RunState { energy, accel_time, stopping }
}

fn rand_vel(r: &mut Lcg) -> Vec3 {
    if r.next() < 0.2 { Vec3::ZERO } else { Vec3::new(r.range(-8.0, 8.0), r.range(-3.0, 3.0), r.range(-8.0, 8.0)) }
}

fn rand_dt(r: &mut Lcg) -> f32 {
    [1.0 / 60.0, 1.0 / 120.0, 0.05, 0.0][(r.next() * 4.0) as usize % 4]
}

fn st3(st: &RunState) -> String { format!("{},{},{}", st.energy, st.accel_time, st.stopping) }

/// kind 0 = sprint_accel, 1 = walk_accel, 2 = controller_accel.
/// Columns: kind, state in (3), forward (3), right (3), a_forward, a_strafe, turn, vel (3), dt,
/// falling, then accel out (3), state out (3), vel out (3): 27 in all.
pub fn accel_csv() -> String {
    let loco = Locomotion::default();
    let mut r = Lcg(0x10C0);
    let mut s = String::from("kind,e,at,stop,fx,fy,fz,rx,ry,rz,af,as,turn,vx,vy,vz,dt,falling,ax,ay,az,e2,at2,stop2,vx2,vy2,vz2\n");
    for n in 0..3000 {
        let kind = n % 3;
        let (st0, i, vel0, dt) = (rand_state(&mut r), rand_input(&mut r), rand_vel(&mut r), rand_dt(&mut r));
        let falling = kind != 2 && r.next() < 0.3;
        let mut st = st0;
        let mut vel = vel0;
        let a = match kind {
            0 => loco.sprint_accel(&mut st, &i, vel, dt, falling),
            1 => loco.walk_accel(&mut st, &i, vel, dt, falling),
            _ => loco.controller_accel(&mut st, &i, &mut vel, dt),
        };
        s += &format!(
            "{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
            kind, st3(&st0), f(i.forward), f(i.right), i.a_forward, i.a_strafe, i.turn, f(vel0), dt, falling as u8,
            f(a), st3(&st), f(vel)
        );
    }
    s
}

/// Columns: vel (3), accel (3), dt, speed_mod, friction, then v out (3): 12 in all.
pub fn velocity_csv() -> String {
    let loco = Locomotion::default();
    let mut r = Lcg(0xCA1C);
    let mut s = String::from("vx,vy,vz,ax,ay,az,dt,mod,fric,ox,oy,oz\n");
    for _ in 0..2000 {
        let vel = rand_vel(&mut r);
        let accel = if r.next() < 0.4 { Vec3::ZERO } else { Vec3::new(r.range(-80.0, 80.0), r.range(-5.0, 5.0), r.range(-80.0, 80.0)) };
        let dt = rand_dt(&mut r);
        let speed_mod = [1.0, 0.2, 0.34][(r.next() * 3.0) as usize % 3];
        let friction = [loco.ground_friction, loco.ground_friction * 0.1, 0.0][(r.next() * 3.0) as usize % 3];
        let v = loco.calc_velocity(vel, accel, dt, speed_mod, friction);
        s += &format!("{},{},{},{},{},{}\n", f(vel), f(accel), dt, speed_mod, friction, f(v));
    }
    s
}
```

The accel row has 27 columns: kind 1, state 3, forward 3, right 3, a_forward/a_strafe/turn 3, vel 3, dt 1, falling 1, accel 3, state 3, vel 3. They're written by 13 `{}`s.

In `tools/golden/src/main.rs`, add `mod locomotion;`, and before `common::write_reference(&dir);` add:

```rust
    common::write(&dir, "curves.csv", locomotion::curves_csv());
    common::write(&dir, "loco_accel.csv", locomotion::accel_csv());
    common::write(&dir, "loco_velocity.csv", locomotion::velocity_csv());
```

Run: `cargo run --release` in `tools/golden`
Expected: the three new files are written. `git diff --exit-code -- RunlineCore/tests/golden/*.csv` exits 0 (no old file changed). `loco_accel.csv` has 3001 lines and 27 columns; `loco_velocity.csv` has 2001 lines and 12 columns.

- [ ] **Step 7: Write the golden replays, then run them**

Add `#include "runline/Locomotion.h"` to `golden_tests.cpp`, and at its end:

```cpp
TEST_CASE("golden: locomotion curves match faith_move") {
    const auto rows = golden::Require("curves.csv", 800, 3);
    const Locomotion loco;
    const Curve speed{SPEED_CURVE};
    golden::Report rep;
    size_t point = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r[0] == 0.0f) {
            REQUIRE(point < loco.accelCurve.points.size());
            rep.Float(i, "point.x", r[1], loco.accelCurve.points[point].first);
            rep.Float(i, "point.y", r[2], loco.accelCurve.points[point].second);
            ++point;
        } else {
            rep.Float(i, r[0] == 1.0f ? "speed.eval" : "accel.eval", r[2], (r[0] == 1.0f ? speed : loco.accelCurve).Eval(r[1]));
        }
    }
    CHECK(point == loco.accelCurve.points.size());
    rep.Finish();
}

TEST_CASE("golden: sprint, walk and controller acceleration match faith_move") {
    const auto rows = golden::Require("loco_accel.csv", 3000, 27);
    const Locomotion loco;
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        RunState st{r[1], r[2], r[3]};
        const RunInput in{golden::V(r, 4), golden::V(r, 7), r[10], r[11], r[12]};
        Vec3 vel = golden::V(r, 13);
        const float dt = r[16];
        const bool falling = r[17] != 0.0f;
        const int kind = static_cast<int>(r[0]);
        const Vec3 a = kind == 0 ? loco.SprintAccel(st, in, vel, dt, falling)
                     : kind == 1 ? loco.WalkAccel(st, in, vel, dt, falling)
                                 : loco.ControllerAccel(st, in, vel, dt);
        rep.Vec(i, "accel", r, 18, a);
        rep.Float(i, "energy", r[21], st.energy);
        rep.Float(i, "accelTime", r[22], st.accelTime);
        rep.Float(i, "stopping", r[23], st.stopping);
        rep.Vec(i, "vel", r, 24, vel);
    }
    rep.Finish();
}

TEST_CASE("golden: CalcVelocity matches faith_move, braking and dt = 0 included") {   // Review Focus 4
    const auto rows = golden::Require("loco_velocity.csv", 2000, 12);
    const Locomotion loco;
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        rep.Vec(i, "v", r, 9, loco.CalcVelocity(golden::V(r, 0), golden::V(r, 3), r[6], r[7], r[8]));
    }
    rep.Finish();
}
```

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 60 test cases.

Mutation checks (revert after each):
1. In `RoundAccel`, change `+ 0.5f` to `+ 0.4f`. Expected: the acceleration golden FAILS.
2. In `CalcVelocity`, change `FMin(left, 0.03f)` to `FMin(left, 0.02f)`. Expected: the CalcVelocity golden FAILS.
3. In `ControllerAccel`, change `st.accelTime < 0.15f` to `< 0.10f`. Expected: the acceleration golden FAILS (tap-stop rows).

Revert all three and re-run: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add RunlineCore tools/golden/src
git commit -m "M1b: port locomotion (sprint curve, ground acceleration, braking) with golden parity

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Tuning (every feel number)

**Files:**
- Create: `RunlineCore/include/runline/Tuning.h`, `RunlineCore/src/Tuning.cpp`, `RunlineCore/tests/tuning_tests.cpp`, `tools/golden/src/tuning.rs`, `RunlineCore/tests/golden/tuning.txt` (generated)
- Modify: `RunlineCore/CMakeLists.txt`, `tools/golden/src/main.rs`

**Interfaces:**
- Consumes: `Locomotion` (Task 3).
- Produces:
  - `struct MeleeClips` (12 floats, the defaults of `tuning.rs:559-576`)
  - `struct Tuning`, with every field of `tuning.rs:11-357` except `turn_curves` and `climb_curves`; those are `None` by default and come with the turn and climb code in M1c or later
  - `float Tuning::SpeedForHeight(float height) const`
  - `std::vector<std::pair<std::string, float>> TuningFields(const Tuning&)`, listing every float and bool field (bools as 0/1) under its **Rust** name. Nested fields are written `loco.max_base` and `melee_clips.punch_start`; array elements `takedown_clips[0]`; `kill_y` appears only when set.

- [ ] **Step 1: Write the Rust field dump, `tools/golden/src/tuning.rs`**

```rust
//! Tuning::default(), every float and bool field by its Rust name, from the Debug output, so the
//! C++ port can be checked field by field without a hand-written list on this side.
use faith_move::tuning::Tuning;

/// A level of nesting in the pretty Debug output, and how it lists values.
#[derive(Clone, PartialEq)]
enum Kind {
    /// A struct (or a skipped tuple): `name: value` lines.
    Fields,
    /// An array: bare values, numbered.
    Array(usize),
    /// `Some(` around one bare value.
    Some,
}

/// Walks `{:#?}` output: `name: value,` becomes `name value`; nested structs prefix their fields
/// (`loco.max_base`); array elements are numbered (`takedown_clips[0]`); `Some(x)` is x; bools
/// are 0/1; `None` and the accel curve's points are skipped.
pub fn txt() -> String {
    let debug = format!("{:#?}", Tuning::default());
    let mut out = String::new();
    let mut stack: Vec<(String, Kind)> = vec![];
    let prefix = |st: &[(String, Kind)]| -> String { st.iter().map(|(n, _)| format!("{n}.")).collect() };
    for raw in debug.lines().skip(1) {
        let line = raw.trim().trim_end_matches(',');
        if line == "}" || line == "]" || line == ")" {
            stack.pop();
            continue;
        }
        let in_curve = stack.iter().any(|(n, _)| n == "accel_curve");
        if let Some((name, value)) = line.split_once(": ") {
            let kind = match value {
                "[" => Some(Kind::Array(0)),
                "Some(" => Some(Kind::Some),
                v if v.ends_with('{') || v.ends_with('(') => Some(Kind::Fields),
                _ => None,
            };
            if let Some(k) = kind {
                stack.push((name.to_string(), k));
                continue;
            }
            if in_curve {
                continue;
            }
            let v = match value { "true" => "1", "false" => "0", other => other };
            if v.parse::<f32>().is_ok() {
                out += &format!("{}{name} {v}\n", prefix(&stack));
            }
        } else if line.ends_with('[') || line.ends_with('(') || line.ends_with('{') {
            stack.push((String::new(), Kind::Fields));
        } else if !in_curve && line.parse::<f32>().is_ok() {
            let n = stack.len();
            match stack.last().cloned() {
                Some((name, Kind::Array(i))) => {
                    stack[n - 1].1 = Kind::Array(i + 1);
                    out += &format!("{}{name}[{i}] {line}\n", prefix(&stack[..n - 1]));
                }
                Some((name, Kind::Some)) => out += &format!("{}{name} {line}\n", prefix(&stack[..n - 1])),
                _ => {}
            }
        }
    }
    out
}
```

In `main.rs`, add `mod tuning;`, and before `common::write_reference(&dir);` add `common::write(&dir, "tuning.txt", tuning::txt());`.

Run: `cargo run --release` in `tools/golden`
Expected: `tuning.txt` is written. `grep -c . RunlineCore/tests/golden/tuning.txt` gives about 137 + 2 (bools) + 14 (loco scalars) + 12 (melee_clips) + 7 (array elements) + 1 (kill_y, if `Some` by default). Spot-check that `gravity`, `loco.max_base`, `melee_clips.punch_start` and `takedown_clips[0]` are present. If the Debug layout differs from what the walker expects (for example arrays printed on one line), adjust the walker until every field appears once, and record a `Ruling:`. Old goldens are unchanged: `git diff --exit-code -- RunlineCore/tests/golden/*.csv` exits 0.

- [ ] **Step 2: Write the failing tests, `RunlineCore/tests/tuning_tests.cpp`**

```cpp
#include <doctest/doctest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include "runline/Tuning.h"
using namespace runline;

TEST_CASE("golden: every Tuning default matches faith_move, bit for bit, with nothing missing") {   // Review Focus 5
    std::ifstream in(std::string(RUNLINE_GOLDEN_DIR) + "/tuning.txt");
    REQUIRE_MESSAGE(in.good(), "tuning.txt missing; run cargo run --release in tools/golden");
    std::map<std::string, float> want;
    std::string name, value;
    while (in >> name >> value) want[name] = std::stof(value);
    REQUIRE(want.size() >= 150);

    std::map<std::string, float> got;
    for (const auto& [n, v] : TuningFields(Tuning{})) {
        CAPTURE(n);
        CHECK_MESSAGE(got.count(n) == 0, "listed twice");
        got[n] = v;
    }
    for (const auto& [n, v] : want) {
        CAPTURE(n);
        REQUIRE_MESSAGE(got.count(n) == 1, "missing from the C++ port");
        CHECK(std::bit_cast<std::uint32_t>(got[n]) == std::bit_cast<std::uint32_t>(v));
    }
    for (const auto& [n, v] : got) {
        CAPTURE(n);
        CHECK_MESSAGE(want.count(n) == 1, "not a field of faith_move's Tuning");
    }
}

TEST_CASE("SpeedForHeight is the launch speed to rise a height under gravity") {
    const Tuning t;
    CHECK(t.SpeedForHeight(0.0f) == 0.0f);
    CHECK(t.SpeedForHeight(-1.0f) == 0.0f);                       // never negative
    CHECK(t.SpeedForHeight(1.2f) == doctest::Approx(std::sqrt(2.0f * t.gravity * 1.2f)));
}
```

Add `src/Tuning.cpp` to the library and `tests/tuning_tests.cpp` to the tests in CMake. Create a placeholder `RunlineCore/src/Tuning.cpp` containing `#include "runline/Tuning.h"`, and a placeholder `Tuning.h` containing `#pragma once`.

- [ ] **Step 3: Run them and check they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: build errors, `Tuning` undeclared.

- [ ] **Step 4: Port `tuning.rs` into `Tuning.h` and `Tuning.cpp`**

`Tuning.h` starts:

```cpp
#pragma once
// Every number that affects feel. Port of faith_move::tuning. Values marked `ME:` are Mirror's
// Edge's own (read from TdGame.u); `guess` ones live in native code or animation data.
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "runline/Locomotion.h"

namespace runline {

/// Unreal units (cm, or cm/s) to metres.
constexpr float Uu(float v) { return v / 100.0f; }

/// How long each attack's clips play (seconds, at the rate the move plays them).
struct MeleeClips {
    /// TdMove_Melee: MeleeStart, MeleeHit, MeleeMissed (Left), at 1.5 x.
    float punchStart = 0.25f;
    // ... every field of tuning.rs:539-576, in order, same defaults
};

struct Tuning {
    float halfWidth = Uu(30.0f);
    float standHeight = Uu(180.0f);
    // ... every field of tuning.rs:11-357 in order, each initialised with its default
    //     expression from tuning.rs:362-527, carrying the doc comments
    Locomotion loco{};
    std::optional<float> killY;          // Option<f32>
    std::array<float, 4> takedownClips;  // [f32; 4]
    std::array<float, 3> airBargeClips;  // [f32; 3]
    MeleeClips meleeClips{};
    // turn_curves / climb_curves: None by default; ported with the turn/climb code.

    /// Launch speed needed to rise `height` metres under normal gravity.
    float SpeedForHeight(float height) const { return std::sqrt(2.0f * gravity * FMax(height, 0.0f)); }
};

/// Every float and bool field under its Rust name (bools as 0/1), for the golden check.
std::vector<std::pair<std::string, float>> TuningFields(const Tuning& t);

} // namespace runline
```

Fill in the `...` by porting the Rust source in order, following the porting convention in Global Constraints. **The structure above is a skeleton: the real header lists every field.** `Tuning.cpp` implements `TuningFields` with one line per field, for example:

```cpp
std::vector<std::pair<std::string, float>> TuningFields(const Tuning& t) {
    std::vector<std::pair<std::string, float>> f;
    f.push_back({"half_width", t.halfWidth});
    f.push_back({"stand_height", t.standHeight});
    // ... one line per float field, Rust name first
    f.push_back({"respawn_on_death", t.respawnOnDeath ? 1.0f : 0.0f});
    if (t.killY) f.push_back({"kill_y", *t.killY});
    for (int i = 0; i < 4; ++i) f.push_back({"takedown_clips[" + std::to_string(i) + "]", t.takedownClips[i]});
    f.push_back({"loco.max_base", t.loco.maxBase});
    f.push_back({"melee_clips.punch_start", t.meleeClips.punchStart});
    // ... and the rest of loco.* and melee_clips.*
    return f;
}
```

Write it from the Rust field list, never from the generated `tuning.txt`. The test exists to catch fields the port missed.

- [ ] **Step 5: Run and check it passes**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 62 test cases.

Mutation checks (revert after each):
1. Change one literal in a computed default to `double` by removing its `f`, for example in `gravity`'s expression. Expected: the tuning golden FAILS on `gravity`, or the compiler warns; either way the difference is detected. If neither happens, the expression rounds identically: pick another computed default and record which one you used.
2. Delete one `f.push_back` line from `TuningFields`. Expected: FAILS with "missing from the C++ port".

Revert both and re-run: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add RunlineCore tools/golden/src
git commit -m "M1b: port Tuning (every feel number) with a bit-exact field-by-field check

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Done when

- `powershell -ExecutionPolicy Bypass -File scripts/test.ps1` prints SUCCESS, with 62 test cases.
- `bash tools/trace/make-traces.sh` regenerates byte-identical traces, and the reference clone stays clean.
- Every task's mutation checks failed as expected, and were reverted.

**Next (M1c):** port the controller types (`Input`, `State`, `Event`, `View`) and the core loop (`new`, `step`, `substep`, `ground`, `air`, `jump`, `land`, `integrate`), replaying the five selected traces frame by frame. Each replay test reports the first frame and field that diverge from Rust.
