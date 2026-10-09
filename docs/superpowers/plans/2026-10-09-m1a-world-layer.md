# M1a World Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish porting faith_move's `world.rs` to RunlineCore: glam-exact vector maths, collide-and-slide (`move_axis`, `slide_move`), the wall, ledge and ground probes, and `MeshWorld` (collision against triangles at any angle). Each piece is proven against the Rust original with golden data.

**Architecture:** This is the same pattern as M0. Each function is ported line by line into a focused file. `tools/golden` is split into modules, one per area, and each module writes a CSV of inputs and Rust's outputs. `RunlineCore/tests/golden_tests.cpp` replays every CSV. Shared reading, loading and reporting code moves into `tests/Golden.h`. Triangle scenes are built in Rust and written to CSV, so C++ never needs glam's quaternion maths.

**Tech Stack:** C++20, MSVC with `/fp:strict`, CMake (VS 2022 bundled), doctest 2.5.3, Rust with glam 0.32.1 (pinned), faith-runner at `9470d755`.

**Spec:** `docs/superpowers/specs/2026-10-09-faith-movement-port-design.md` (M1, first part)

## Global Constraints

- RunlineCore: C++20, no Unreal headers, standard library only. Metres, Y up. `float` everywhere.
- **No `std::min`/`std::max` on floats in RunlineCore.** Use `FMin`/`FMax`/`FClamp` (Task 1), which break ties the way Rust and glam do. A grep in Task 1 enforces this.
- Port fidelity: keep faith_move's names (snake_case becomes CamelCase), constants (`TOUCH = 1e-4`, `SKIN = 0.05`, `LIP = 0.05`, `WALKABLE = 0.7`) and operation order. Carry the Rust doc comments over.
- Golden tolerance is `1e-5` for floats. Flags and counts must match exactly. Sweep `t` must also match the **sign of zero**.
- Golden inputs and scenes are generated only in Rust. C++ only reads them.
- Regenerate golden data only from a clean faith-runner checkout at `9470d755`. Commit the CSVs and `REFERENCE`.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- The test command is `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`. The golden command is `cargo run --release` in `tools/golden`; `cargo` lives in `%USERPROFILE%\.cargo\bin`.

## Review Focus

1. **Equal values and ±0 in min, max and clamp** must come out bit-identical to Rust. If they don't, `-0` leaks into results, and later `signbit`/`copysign` logic diverges. Pinned in Task 1 (unit test plus the scalars golden).
2. **Sliding into an inside corner** (two walls at 90°) must stop cleanly: feet unchanged and two normals, not jitter. Pinned in Task 2.
3. **A slide's vertical component is ignored**: `slide_move` only moves horizontally. Pinned in Task 2 (unit test, plus structured golden rows).
4. **A mesh box sunk less than `SKIN` into a surface:** moving further in is blocked at `t = 0`, while moving out or sideways is free. Pinned in Task 4.
5. **Probes given a zero direction or zero reach** return nothing rather than dividing by zero. Pinned in Task 3 (unit tests plus golden rows).

---

## File Structure

```
RunlineCore/include/runline/Vec3.h      Vec3 (glam-exact) + FMin/FMax/FClamp            (modify, T1)
RunlineCore/include/runline/World.h     Aabb (+Center/Size/FromCenterSize), constants    (modify, T1/T2)
RunlineCore/src/BoxWorld.cpp            switch to FMin/FMax; shared TOUCH                 (modify, T1/T2)
RunlineCore/include/runline/Move.h      Body, AxisHit, MoveAxis, SlideMove                (create, T2)
RunlineCore/src/Move.cpp
RunlineCore/include/runline/Probes.h    ClosestOnSegment, ProbeWall, TraceBox, TraceWall,
RunlineCore/src/Probes.cpp              TopsBelow, ColumnTop, FindLedge                   (create, T3)
RunlineCore/include/runline/MeshWorld.h MeshWorld, TriBounds, TriBoxOverlap               (create, T4)
RunlineCore/src/MeshWorld.cpp
RunlineCore/tests/Golden.h              CSV reading, scenes, Report                       (create, T1)
RunlineCore/tests/golden_tests.cpp      every golden replay                               (rewrite T1, extend T2-T4)
RunlineCore/tests/vec3_tests.cpp        (extend, T1)
RunlineCore/tests/world_tests.cpp       (extend, T1)
RunlineCore/tests/move_tests.cpp        (create, T2)
RunlineCore/tests/probes_tests.cpp      (create, T3)
RunlineCore/tests/mesh_world_tests.cpp  (create, T4)
tools/golden/src/main.rs                calls each module                                 (rewrite T1, extend T2-T4)
tools/golden/src/common.rs              Lcg, f(), write(), write_reference()             (create, T1)
tools/golden/src/sweeps.rs              M0's box scene + sweeps, unchanged output         (create, T1)
tools/golden/src/scalars.rs             f32 min/max/clamp ties                            (create, T1)
tools/golden/src/slides.rs              move_axis / slide_move                            (create, T2)
tools/golden/src/probes.rs              probe_wall, trace_box, trace_wall, tops_below,
                                        find_ledge                                        (create, T3)
tools/golden/src/mesh.rs                triangle scene + its goldens                      (create, T4)
```

---

### Task 1: glam-exact maths, golden refactor, and the sign of zero

**Files:**
- Modify: `RunlineCore/include/runline/Vec3.h`, `RunlineCore/include/runline/World.h`, `RunlineCore/src/BoxWorld.cpp`, `RunlineCore/tests/vec3_tests.cpp`, `RunlineCore/tests/world_tests.cpp`
- Rewrite: `RunlineCore/tests/golden_tests.cpp`, `tools/golden/src/main.rs`
- Create: `RunlineCore/tests/Golden.h`, `tools/golden/src/common.rs`, `tools/golden/src/sweeps.rs`, `tools/golden/src/scalars.rs`, `RunlineCore/tests/golden/scalars.csv` (generated)

**Interfaces:**
- Produces:
  - `runline::FMin(float, float)`, `FMax(float, float)`, `FClamp(float x, float lo, float hi)`
  - `Vec3::Zero()`, `UnitX()`, `UnitY()`, `UnitZ()`, `Splat()`
  - Vec3 operators: unary `-`, `/ float`, `+=`, `-=`, `*= float`
  - Vec3 methods: `Dot`, `Cross`, `Length`, `LengthSquared`, `LengthRecip`, `Normalize`, `NormalizeOrZero`
  - `Aabb::Center()`, `Size()`, `FromCenterSize(Vec3, Vec3)`
  - In `Golden.h`, namespace `golden`: `Row`, `Rows`, `ReadCsv`, `Require(name, minRows, cols)`, `V(row, i)`, `BoxScene()`, `Same`, `Near`, `Report` (with `Add`, `Float`, `Flag`, `Vec`, `Finish`), and `CompareHit(Report&, size_t, const std::optional<SweepHit>&, const Row&, size_t at)`
  - In Rust: `common::{Lcg, f, golden_dir, write, write_reference}`, and `sweeps::{SCENE, scene, scene_csv, sweeps_csv, row, anchors}`

- [ ] **Step 1: Write the failing Vec3 and Aabb tests**

Append to `RunlineCore/tests/vec3_tests.cpp` (add `#include <cmath>` after the existing includes, and `using runline::FMin; using runline::FMax; using runline::FClamp;` after `using runline::Vec3;`):

```cpp
TEST_CASE("Vec3 dot, cross, length and normalize, in glam's operation order") {
    Vec3 a{1, 2, 3}, b{4, -5, 6};
    CHECK(a.Dot(b) == 12.0f);                      // 4 - 10 + 18
    CHECK(a.Cross(b) == Vec3{27, 6, -13});         // (2*6 - -5*3, 3*4 - 6*1, 1*-5 - 4*2)
    CHECK(Vec3{3, 4, 0}.Length() == 5.0f);
    CHECK(Vec3{3, 4, 0}.LengthSquared() == 25.0f);
    CHECK(Vec3{0, 0, 2}.Normalize() == Vec3{0, 0, 1});
    CHECK(Vec3{0, 0, 2}.NormalizeOrZero() == Vec3::UnitZ());
    CHECK(Vec3{}.NormalizeOrZero() == Vec3::Zero());   // no direction: zero, not NaN
    CHECK(-a == Vec3{-1, -2, -3});
    CHECK(b / 2.0f == Vec3{2, -2.5f, 3});
    Vec3 c = a; c += b; c -= a; c *= 2.0f;
    CHECK(c == Vec3{8, -10, 12});
}

TEST_CASE("ties go to the second argument, as in glam and Rust") {     // Review Focus 1
    CHECK_FALSE(std::signbit(FMax(-0.0f, 0.0f)));                      // (-0.0f32).max(0.0) is +0
    CHECK_FALSE(std::signbit(Vec3::Min(Vec3{-0.0f, 1, 1}, Vec3{0.0f, 1, 1}).x));
    CHECK(FClamp(5.0f, 0.0f, 1.0f) == 1.0f);
    CHECK(FClamp(-5.0f, 0.0f, 1.0f) == 0.0f);
    CHECK(FMin(1.0f, 2.0f) == 1.0f);
}
```

Append to `RunlineCore/tests/world_tests.cpp`:

```cpp
TEST_CASE("Aabb center, size and from center/size") {
    const Aabb a = Aabb::New({0, 0, 0}, {2, 4, 6});
    CHECK(a.Center() == Vec3{1, 2, 3});
    CHECK(a.Size() == Vec3{2, 4, 6});
    CHECK(Aabb::FromCenterSize({1, 2, 3}, {2, 4, 6}).min == Vec3{0, 0, 0});
    CHECK(Aabb::FromCenterSize({1, 2, 3}, {2, 4, 6}).max == Vec3{2, 4, 6});
}
```

- [ ] **Step 2: Run them and check they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: build errors: `Dot`, `FMax` and `Center` are not members or undeclared.

- [ ] **Step 3: Replace `RunlineCore/include/runline/Vec3.h`**

```cpp
#pragma once
#include <cmath>

namespace runline {

// Scalar min/max/clamp with Rust's f32 tie-breaking: on equal values (including -0 and +0)
// the second argument wins, as glam's Vec3::min/max spell out. The scalars golden test checks
// this against Rust itself.
inline float FMin(float a, float b) { return a < b ? a : b; }
inline float FMax(float a, float b) { return a > b ? a : b; }
/// Rust's f32::clamp: below `lo` gives `lo`, above `hi` gives `hi`.
inline float FClamp(float x, float lo, float hi) {
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return x;
}

// The subset of glam::Vec3 that faith_move uses, in glam 0.32's exact operation order.
// Metres, Y up.
struct Vec3 {
    float x = 0, y = 0, z = 0;

    static constexpr Vec3 Zero() { return {0, 0, 0}; }
    static constexpr Vec3 UnitX() { return {1, 0, 0}; }
    static constexpr Vec3 UnitY() { return {0, 1, 0}; }
    static constexpr Vec3 UnitZ() { return {0, 0, 1}; }
    static constexpr Vec3 Splat(float v) { return {v, v, v}; }

    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    friend Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    friend Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
    friend Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
    friend bool operator==(Vec3 a, Vec3 b) = default;
    Vec3& operator+=(Vec3 b) { return *this = *this + b; }
    Vec3& operator-=(Vec3 b) { return *this = *this - b; }
    Vec3& operator*=(float s) { return *this = *this * s; }

    /// glam's min/max: per component, the second argument wins ties.
    static Vec3 Min(Vec3 a, Vec3 b) { return {FMin(a.x, b.x), FMin(a.y, b.y), FMin(a.z, b.z)}; }
    static Vec3 Max(Vec3 a, Vec3 b) { return {FMax(a.x, b.x), FMax(a.y, b.y), FMax(a.z, b.z)}; }
    /// glam's clamp: self.max(lo).min(hi).
    Vec3 Clamp(Vec3 lo, Vec3 hi) const { return Min(Max(*this, lo), hi); }

    float Dot(Vec3 b) const { return (x * b.x) + (y * b.y) + (z * b.z); }
    Vec3 Cross(Vec3 b) const { return {y * b.z - b.y * z, z * b.x - b.z * x, x * b.y - b.x * y}; }
    float LengthSquared() const { return Dot(*this); }
    float Length() const { return std::sqrt(Dot(*this)); }
    float LengthRecip() const { return 1.0f / Length(); }
    Vec3 Normalize() const { return *this * LengthRecip(); }
    /// Unit length, or zero when there's no direction (glam's normalize_or_zero).
    Vec3 NormalizeOrZero() const {
        const float rcp = LengthRecip();
        return (std::isfinite(rcp) && rcp > 0.0f) ? *this * rcp : Zero();
    }
};

} // namespace runline
```

- [ ] **Step 4: Add the Aabb helpers in `World.h`**

In `struct Aabb`, after `static Aabb New(...)`, add:

```cpp
    static Aabb FromCenterSize(Vec3 center, Vec3 size) {
        const Vec3 h = size * 0.5f;
        return New(center - h, center + h);
    }
    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Size() const { return max - min; }
```

- [ ] **Step 5: Run and check it passes**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 19 test cases (16 + 3 new; the golden file is rewritten in Step 9).

- [ ] **Step 6: Split the golden tool into modules, with identical output**

Create `tools/golden/src/common.rs`:

```rust
//! Shared by every golden module: the random source, number printing, and file writing.
use glam::Vec3;

/// Deterministic LCG, so the files only change when the reference does.
pub struct Lcg(pub u64);
impl Lcg {
    pub fn next(&mut self) -> f32 {
        self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        ((self.0 >> 40) as f32) / ((1u64 << 24) as f32)
    }
    pub fn range(&mut self, lo: f32, hi: f32) -> f32 { lo + (hi - lo) * self.next() }
    /// Snap a third of values to 0 so the zero-delta branch is exercised.
    pub fn axis(&mut self, m: f32) -> f32 { if self.next() < 0.33 { 0.0 } else { self.range(-m, m) } }
}

// `{}` prints an f32 in the shortest form that round-trips, so C++'s stof reads back the same bits.
pub fn f(v: Vec3) -> String { format!("{},{},{}", v.x, v.y, v.z) }

pub fn golden_dir() -> String {
    concat!(env!("CARGO_MANIFEST_DIR"), "/../../RunlineCore/tests/golden").to_string()
}

pub fn write(dir: &str, name: &str, body: String) {
    std::fs::write(format!("{dir}/{name}"), body).unwrap();
    println!("wrote {name}");
}

/// Record which faith-runner commit this data came from, so a regenerated file can be traced.
pub fn write_reference(dir: &str) {
    let clone = concat!(env!("CARGO_MANIFEST_DIR"), "/../../../faith-runner");
    let git = |args: &[&str]| {
        std::process::Command::new("git").arg("-C").arg(clone).args(args).output()
            .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
            .unwrap_or_default()
    };
    let sha = git(&["rev-parse", "HEAD"]);
    assert!(!sha.is_empty(), "can't read faith-runner's commit from {clone}");
    let dirty = if git(&["status", "--porcelain"]).is_empty() { "" } else { " (dirty)" };
    std::fs::write(format!("{dir}/REFERENCE"), format!("faith-runner {sha}{dirty}\n")).unwrap();
    println!("wrote REFERENCE (faith-runner {sha}{dirty})");
}
```

Create `tools/golden/src/sweeps.rs`. Move into it, **unchanged in behaviour**, the current `SCENE`, `row`, `structured` and `touch_band` from `main.rs`. Make `SCENE` and `row` `pub`, change `row`'s, `structured`'s and `touch_band`'s first parameter from `&BoxWorld` to `&dyn World`, and add the functions below. The file starts:

```rust
//! The M0 box scene and BoxWorld::sweep cases (structured, TOUCH band, then random).
use crate::common::{f, Lcg};
use faith_move::world::{Aabb, BoxWorld, World};
use glam::Vec3;
use std::fmt::Write as _;
```

and gains:

```rust
pub fn scene() -> BoxWorld {
    let mut w = BoxWorld::default();
    for (a, b) in SCENE {
        w.add(Aabb::new(Vec3::from(a), Vec3::from(b)));
    }
    w
}

pub fn scene_csv() -> String {
    let mut s = String::from("minx,miny,minz,maxx,maxy,maxz\n");
    for (a, b) in SCENE {
        writeln!(s, "{},{}", f(Vec3::from(a)), f(Vec3::from(b))).unwrap();
    }
    s
}

pub fn sweeps_csv(w: &BoxWorld) -> String {
    let mut out = String::from("hx,hy,hz,sx,sy,sz,dx,dy,dz,hit,t,nx,ny,nz,px,py,pz\n");
    structured(w, &mut out);
    touch_band(w, &mut out);
    let mut r = Lcg(0x5EED);
    for _ in 0..4000 {
        let half = Vec3::new(r.range(0.1, 0.5), r.range(0.3, 1.0), r.range(0.1, 0.5));
        let start = Vec3::new(r.range(-6.0, 6.0), r.range(0.0, 4.0), r.range(-6.0, 6.0));
        let delta = Vec3::new(r.axis(6.0), r.axis(4.0), r.axis(6.0));
        out.push_str(&row(w, half, start, delta));
    }
    out
}

/// Spots on the floor (and the block's top) next to each box: where collisions happen.
pub fn anchors() -> Vec<Vec3> {
    vec![
        Vec3::new(1.7, 0.0, 0.0),   // touching the wall
        Vec3::new(1.7, 0.0, 1.3),   // at the wall's end
        Vec3::new(-1.7, 0.0, 3.0),  // touching the block
        Vec3::new(-3.0, 1.2, 3.0),  // on the block
        Vec3::new(3.7, 0.0, 3.7),   // at the pillar's corner
        Vec3::new(-0.5, 0.0, -5.0), // by the floor seam
        Vec3::new(0.0, 0.0, -4.5),  // under the pipe
    ]
}
```

Create `tools/golden/src/scalars.rs`:

```rust
//! Rust's f32 min/max/clamp on ties (equal values, -0 against +0), evaluated at run time.
use std::fmt::Write as _;
use std::hint::black_box;

pub fn csv() -> String {
    let vals = [-0.0f32, 0.0, 1.0, -1.0, 0.5, 1e-9];
    let mut s = String::from("a,b,min,max,clamp01,clampz\n");
    for a in vals {
        for b in vals {
            let (a, b) = (black_box(a), black_box(b));
            writeln!(s, "{},{},{},{},{},{}", a, b, a.min(b), a.max(b), a.clamp(0.0, 1.0), a.clamp(-0.0, 0.0)).unwrap();
        }
    }
    s
}
```

Replace `tools/golden/src/main.rs`:

```rust
//! Writes reference results from faith-runner's faith_move for RunlineCore's golden tests.
mod common;
mod scalars;
mod sweeps;

fn main() {
    let dir = common::golden_dir();
    std::fs::create_dir_all(&dir).unwrap();
    let boxes = sweeps::scene();
    common::write(&dir, "scene.csv", sweeps::scene_csv());
    common::write(&dir, "sweeps.csv", sweeps::sweeps_csv(&boxes));
    common::write(&dir, "scalars.csv", scalars::csv());
    common::write_reference(&dir);
}
```

- [ ] **Step 7: Regenerate, and prove the refactor changed nothing**

Run: `cargo run --release` in `tools/golden`, then `git diff --exit-code -- RunlineCore/tests/golden/scene.csv RunlineCore/tests/golden/sweeps.csv RunlineCore/tests/golden/REFERENCE`
Expected: the tool prints `wrote scene.csv`, `wrote sweeps.csv`, `wrote scalars.csv` and `wrote REFERENCE (faith-runner 9470d755…)` with no `(dirty)`. git diff exits 0 (no changes). `scalars.csv` has 37 lines. If `sweeps.csv` changed, the move wasn't faithful: diff the old `main.rs` against `sweeps.rs`.

- [ ] **Step 8: Write `RunlineCore/tests/Golden.h`**

```cpp
#pragma once
// Shared by the golden tests: reading the CSVs tools/golden writes, loading its scenes, and
// reporting the first mismatches by row and field.
#include <doctest/doctest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "runline/BoxWorld.h"

namespace golden {

using Row = std::vector<float>;
using Rows = std::vector<Row>;

inline Rows ReadCsv(const std::string& name) {
    std::ifstream in(std::string(RUNLINE_GOLDEN_DIR) + "/" + name);
    Rows rows;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        Row row;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) row.push_back(std::stof(cell));
        rows.push_back(row);
    }
    return rows;
}

/// The rows of `name`, failing loudly if it's missing, short, or a row has the wrong width.
inline Rows Require(const std::string& name, size_t minRows, size_t cols) {
    Rows rows = ReadCsv(name);
    REQUIRE_MESSAGE(rows.size() >= minRows, name << " missing or short; run cargo run --release in tools/golden");
    for (const Row& r : rows)
        REQUIRE_MESSAGE(r.size() == cols, name << " has a row of width " << r.size() << ", expected " << cols);
    return rows;
}

inline runline::Vec3 V(const Row& r, size_t i) { return {r[i], r[i + 1], r[i + 2]}; }

/// The box scene tools/golden used (scene.csv), so it can't drift from the Rust side.
inline runline::BoxWorld BoxScene() {
    runline::BoxWorld w;
    for (const Row& r : Require("scene.csv", 1, 6)) w.Add(runline::Aabb::New(V(r, 0), V(r, 3)));
    return w;
}

inline bool Same(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }
inline bool Near(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

/// Collects mismatches; the first ten are logged with their row and field.
class Report {
public:
    void Add(size_t row, const std::string& what) {
        if (++count_ <= 10) MESSAGE("row " << row << " (csv line " << row + 2 << "): " << what);
    }
    void Float(size_t row, const std::string& field, float want, float got) {
        if (!Near(want, got)) Add(row, field + ": want " + std::to_string(want) + ", got " + std::to_string(got));
    }
    void Flag(size_t row, const std::string& field, float want, bool got) {
        const bool w = want != 0.0f;
        if (w != got) Add(row, field + ": want " + (w ? "1" : "0") + ", got " + (got ? "1" : "0"));
    }
    void Vec(size_t row, const std::string& field, const Row& r, size_t i, runline::Vec3 got) {
        Float(row, field + ".x", r[i], got.x);
        Float(row, field + ".y", r[i + 1], got.y);
        Float(row, field + ".z", r[i + 2], got.z);
    }
    void Finish() const { CHECK(count_ == 0); }

private:
    int count_ = 0;
};

/// Compare a sweep result with the 8 columns at `at`: hit, t, normal xyz, point xyz.
inline void CompareHit(Report& rep, size_t row, const std::optional<runline::SweepHit>& h, const Row& r, size_t at) {
    rep.Flag(row, "hit", r[at], h.has_value());
    if (!h || r[at] == 0.0f) return;
    rep.Float(row, "t", r[at + 1], h->t);
    if (std::signbit(r[at + 1]) != std::signbit(h->t)) rep.Add(row, "t: sign of zero differs from Rust");
    rep.Vec(row, "normal", r, at + 2, h->normal);
    rep.Vec(row, "point", r, at + 5, h->point);
}

} // namespace golden
```

- [ ] **Step 9: Rewrite `RunlineCore/tests/golden_tests.cpp`**

```cpp
#include "Golden.h"
#include <cmath>
using namespace runline;

TEST_CASE("golden: Rust's f32 min, max and clamp, ties included") {   // Review Focus 1
    const auto rows = golden::Require("scalars.csv", 36, 6);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const float got[] = {FMin(r[0], r[1]), FMax(r[0], r[1]), FClamp(r[0], 0.0f, 1.0f), FClamp(r[0], -0.0f, 0.0f)};
        const char* names[] = {"min", "max", "clamp01", "clampz"};
        for (int k = 0; k < 4; ++k)
            if (!golden::Same(got[k], r[2 + k]))
                rep.Add(i, std::string(names[k]) + ": want " + std::to_string(r[2 + k]) + ", got " + std::to_string(got[k]));
    }
    rep.Finish();
}

TEST_CASE("golden: BoxWorld::Sweep matches faith_move") {
    const BoxWorld w = golden::BoxScene();
    const auto rows = golden::Require("sweeps.csv", 1000, 17);   // Review Focus 4 (M0)
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        golden::CompareHit(rep, i, w.Sweep(golden::V(r, 0), golden::V(r, 3), golden::V(r, 6)), r, 9);
    }
    rep.Finish();
}

TEST_CASE("golden data records the faith-runner commit it came from") {
    std::ifstream in(std::string(RUNLINE_GOLDEN_DIR) + "/REFERENCE");
    std::string line;
    std::getline(in, line);
    REQUIRE_MESSAGE(line.rfind("faith-runner ", 0) == 0, "REFERENCE missing; run cargo run in tools/golden");
    CHECK(line.size() >= 13 + 40);                    // a full 40-hex commit SHA
    CHECK(line.find("dirty") == std::string::npos);   // made from a clean checkout
}
```

- [ ] **Step 10: Run it and check the sweep test fails on the sign of zero**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: FAIL in `golden: BoxWorld::Sweep matches faith_move`, with messages `t: sign of zero differs from Rust`. `BoxWorld.cpp` still uses `std::max(enter, 0.0f)`, which keeps `-0`. The scalars test should pass. **If the scalars test fails**, the golden data is the authority: change `FMin`/`FMax`'s tie handling to match what the CSV shows, and record a `Ruling:` in the ledger.

- [ ] **Step 11: Switch BoxWorld to the Rust-exact helpers**

In `RunlineCore/src/BoxWorld.cpp`, replace `exitS = std::min(exitS, sx);` with `exitS = FMin(exitS, sx);`, and `const float t = std::max(enter, 0.0f);` with `const float t = FMax(enter, 0.0f);`. Remove `#include <limits>` only if nothing else needs it (the infinities do, so keep it).

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 20 test cases.

Run: `git grep -nE "std::(min|max)\(" -- RunlineCore/src RunlineCore/include`
Expected: no output.

- [ ] **Step 12: Commit**

```bash
git add RunlineCore tools/golden/src
git commit -m "M1a: glam-exact Vec3, Rust tie-breaking min/max, golden refactor

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Body, MoveAxis and SlideMove (collide-and-slide)

**Files:**
- Modify: `RunlineCore/include/runline/World.h` (shared constants), `RunlineCore/src/BoxWorld.cpp` (use the shared `TOUCH`; rename its private `AxisHit` to `SlabHit`), `RunlineCore/CMakeLists.txt`, `RunlineCore/tests/golden_tests.cpp`, `tools/golden/src/main.rs`
- Create: `RunlineCore/include/runline/Move.h`, `RunlineCore/src/Move.cpp`, `RunlineCore/tests/move_tests.cpp`, `tools/golden/src/slides.rs`, `RunlineCore/tests/golden/{slides,move_axis}.csv` (generated)

**Interfaces:**
- Consumes: Task 1's Vec3, Aabb and golden helpers.
- Produces:
  - In `World.h`: `inline constexpr float TOUCH = 1e-4f, SKIN = 0.05f, LIP = 0.05f, WALKABLE = 0.7f;`
  - `struct Body { float halfWidth; float height; Aabb AabbAt(Vec3 feet) const; float ReachToward(Vec3 n) const; }`
  - `struct AxisHit { bool blocked = false; Vec3 normal{}; }`
  - `AxisHit MoveAxis(const World&, Body, Vec3& feet, int axis, float d)`
  - `std::vector<Vec3> SlideMove(const World&, Body, Vec3& feet, Vec3 delta)`
  - In Rust: `slides::{BODIES, dirs8, slides_csv, move_axis_csv}`

- [ ] **Step 1: Write the failing tests, `RunlineCore/tests/move_tests.cpp`**

```cpp
#include <doctest/doctest.h>
#include "runline/BoxWorld.h"
#include "runline/Move.h"
using namespace runline;

namespace {
const Body kStand{0.3f, 1.8f};
BoxWorld FloorAndWall() {   // floor top at y = 0, a long wall from x = 2
    BoxWorld w;
    w.Add(Aabb::New({-10, -1, -10}, {10, 0, 10})).Add(Aabb::New({2, 0, -10}, {3, 3, 10}));
    return w;
}
} // namespace

TEST_CASE("Body's box and reach") {
    const Aabb b = kStand.AabbAt({1, 0, 2});
    CHECK(b.min == Vec3{0.7f, 0, 1.7f});
    CHECK(b.max == Vec3{1.3f, 1.8f, 2.3f});
    CHECK(kStand.ReachToward({1, 0, 0}) == doctest::Approx(0.3f));
    CHECK(kStand.ReachToward({0.6f, 0, 0.8f}) == doctest::Approx(0.42f));
}

TEST_CASE("MoveAxis moves freely, or stops at the first thing in the way") {
    const auto w = FloorAndWall();
    Vec3 feet{0, 0, 0};
    CHECK_FALSE(MoveAxis(w, kStand, feet, 0, 1.0f).blocked);
    CHECK(feet.x == doctest::Approx(1.0f));
    const AxisHit h = MoveAxis(w, kStand, feet, 0, 4.0f);
    CHECK(h.blocked);
    CHECK(h.normal == Vec3{-1, 0, 0});
    CHECK(feet.x == doctest::Approx(1.7f));
    CHECK_FALSE(MoveAxis(w, kStand, feet, 2, 0.0f).blocked);   // a zero move does nothing
}

TEST_CASE("SlideMove slides along a wall it runs into at an angle") {
    const auto w = FloorAndWall();
    Vec3 feet{1.7f, 0, 0};                                     // touching the wall
    const auto ns = SlideMove(w, kStand, feet, {1, 0, 1});
    REQUIRE(ns.size() == 1);
    CHECK(ns[0] == Vec3{-1, 0, 0});
    CHECK(feet.x == doctest::Approx(1.7f));
    CHECK(feet.z == doctest::Approx(1.0f));
}

TEST_CASE("SlideMove into an inside corner stops dead with two normals") {   // Review Focus 2
    BoxWorld w;
    w.Add(Aabb::New({2, 0, -10}, {3, 3, 10})).Add(Aabb::New({-10, 0, 2}, {10, 3, 3}));
    Vec3 feet{1.7f, 0, 1.7f};
    const auto ns = SlideMove(w, kStand, feet, {1, 0, 1});
    REQUIRE(ns.size() == 2);
    CHECK(ns[0] == Vec3{-1, 0, 0});
    CHECK(ns[1] == Vec3{0, 0, -1});
    CHECK(feet == Vec3{1.7f, 0, 1.7f});
}

TEST_CASE("SlideMove ignores the vertical part of the move") {             // Review Focus 3
    const auto w = FloorAndWall();
    Vec3 feet{0, 0, 0};
    CHECK(SlideMove(w, kStand, feet, {1, 5, 0}).empty());
    CHECK(feet.x == doctest::Approx(1.0f));
    CHECK(feet.y == 0.0f);
}
```

In `RunlineCore/CMakeLists.txt`, make the library line `add_library(RunlineCore STATIC src/BoxWorld.cpp src/Move.cpp)` and add `tests/move_tests.cpp` to `add_executable(RunlineCoreTests ...)`. Create an empty `RunlineCore/src/Move.cpp` containing `#include "runline/Move.h"`, and an empty `RunlineCore/include/runline/Move.h` containing `#pragma once`.

- [ ] **Step 2: Run them and check they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: build errors, `Body` and `MoveAxis` undeclared.

- [ ] **Step 3: Share the constants, and rename BoxWorld's private `AxisHit`**

In `World.h`, after the includes and inside `namespace runline`, before `struct Aabb`, add:

```cpp
/// How far a box may be inside something and still only be touching it.
inline constexpr float TOUCH = 1e-4f;
/// How far a box may have sunk into a triangle surface and still be held by it (MeshWorld).
inline constexpr float SKIN = 0.05f;
/// How high a lip in the floor the box is lifted over (SlideMove).
inline constexpr float LIP = 0.05f;
/// Surfaces whose normal points at least this far up are ground you can walk on (about 45 degrees).
inline constexpr float WALKABLE = 0.7f;
```

In `BoxWorld.cpp`, delete its local `constexpr float TOUCH = 1e-4f;` (and its comment), and rename the private `struct AxisHit` and both of its uses to `SlabHit`.

- [ ] **Step 4: Write `RunlineCore/include/runline/Move.h`**

```cpp
#pragma once
#include <vector>
#include "runline/World.h"

namespace runline {

/// Player collider: feet position is the bottom-center of the box.
struct Body {
    float halfWidth;
    float height;

    Aabb AabbAt(Vec3 feet) const {
        return Aabb::New(feet - Vec3{halfWidth, 0.0f, halfWidth}, feet + Vec3{halfWidth, height, halfWidth});
    }
    /// How far the box reaches out from its centre line toward horizontal direction `n`.
    float ReachToward(Vec3 n) const { return halfWidth * (std::fabs(n.x) + std::fabs(n.z)); }
};

/// Result of a single-axis move.
struct AxisHit {
    bool blocked = false;
    /// What stopped it (when blocked).
    Vec3 normal{};
};

/// Move `feet` along one axis by `d`, stopping at the first thing in the way.
AxisHit MoveAxis(const World& world, Body body, Vec3& feet, int axis, float d);

/// Move `feet` horizontally by `delta`, sliding along whatever it runs into (up to three
/// contacts: the leftover motion loses its part into each surface). Returns the horizontal
/// normals of what it touched.
std::vector<Vec3> SlideMove(const World& world, Body body, Vec3& feet, Vec3 delta);

} // namespace runline
```

- [ ] **Step 5: Write `RunlineCore/src/Move.cpp`, a line-for-line port**

```cpp
#include "runline/Move.h"

namespace runline {

AxisHit MoveAxis(const World& world, Body body, Vec3& feet, int axis, float d) {
    if (d == 0.0f) return {};
    const Aabb me = body.AabbAt(feet);
    Vec3 delta{};
    delta[axis] = d;
    if (const auto h = world.Sweep(me.Size() * 0.5f, me.Center(), delta)) {
        feet[axis] += d * h->t;
        return {true, h->normal};
    }
    feet[axis] += d;
    return {};
}

std::vector<Vec3> SlideMove(const World& world, Body body, Vec3& feet, Vec3 delta) {
    Vec3 rem{delta.x, 0.0f, delta.z};
    std::vector<Vec3> normals;
    for (int i = 0; i < 3; ++i) {
        if (rem.LengthSquared() < 1e-12f) break;
        const Aabb me = body.AabbAt(feet);
        const auto h = world.Sweep(me.Size() * 0.5f, me.Center(), rem);
        if (!h) {
            feet += rem;
            break;
        }
        const float progress = rem.Length() * h->t;
        feet += rem * h->t;
        rem *= 1.0f - h->t;
        if (h->normal.y >= WALKABLE) {
            // Ground you can walk up: carry on along it, up the slope.
            const float into = rem.Dot(h->normal);
            if (into < 0.0f) {
                rem -= h->normal * into;
            } else if (progress < 0.001f) {
                // Stopped dead by the edge of something flat just above the feet (a seam where
                // two floors meet a centimetre apart): over it, as a foot would. The walk
                // settles her onto it after.
                const AxisHit lifted = MoveAxis(world, body, feet, 1, LIP);
                if (!lifted.blocked) continue;
                break;
            }
            continue;
        }
        const Vec3 n = Vec3{h->normal.x, 0.0f, h->normal.z}.NormalizeOrZero();
        if (n == Vec3::Zero()) break;
        normals.push_back(n);
        const float into = rem.Dot(n);
        if (into < 0.0f) rem -= n * into;
    }
    return normals;
}

} // namespace runline
```

- [ ] **Step 6: Run and check it passes**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 25 test cases.

- [ ] **Step 7: Generate the slide goldens**

Create `tools/golden/src/slides.rs`:

```rust
//! move_axis and slide_move: collide-and-slide, the base of every ground move.
use crate::common::{f, Lcg};
use faith_move::world::{move_axis, slide_move, Body, World};
use glam::Vec3;

/// Standing and crouched (TdMove.ShrinkCollision) bodies.
pub const BODIES: [Body; 2] = [Body { half_width: 0.3, height: 1.8 }, Body { half_width: 0.3, height: 1.22 }];

/// Eight horizontal directions, 45 degrees apart.
pub fn dirs8() -> Vec<Vec3> {
    (0..8)
        .map(|i| {
            let a = i as f32 * std::f32::consts::FRAC_PI_4;
            Vec3::new(a.cos(), 0.0, a.sin())
        })
        .collect()
}

fn slide_row(w: &dyn World, body: Body, feet: Vec3, delta: Vec3) -> String {
    let mut out = feet;
    let ns = slide_move(w, body, &mut out, delta);
    let mut cols = vec![body.half_width.to_string(), body.height.to_string(), f(feet), f(delta), f(out), ns.len().to_string()];
    for i in 0..3 {
        cols.push(f(ns.get(i).copied().unwrap_or(Vec3::ZERO)));
    }
    cols.join(",") + "\n"
}

/// Feet on the ground at each anchor (touching, and sunk 5e-5), in every direction at three
/// lengths, plus a move with a vertical part; then random feet and moves.
pub fn slides_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,dx,dy,dz,ox,oy,oz,count,n1x,n1y,n1z,n2x,n2y,n2z,n3x,n3y,n3z\n");
    for body in BODIES {
        for &a in anchors {
            for d in dirs8() {
                for len in [0.1f32, 1.0, 3.0] {
                    for sink in [0.0f32, 5e-5] {
                        s.push_str(&slide_row(w, body, a - Vec3::Y * sink, d * len));
                    }
                }
            }
            s.push_str(&slide_row(w, body, a, Vec3::new(1.0, 5.0, 0.0))); // the y part is ignored
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..3000 {
        let body = BODIES[(r.next() * 2.0) as usize % 2];
        let y = if r.next() < 0.7 { 0.0 } else { r.range(0.0, 3.0) };
        let feet = Vec3::new(r.range(-6.0, 6.0), y, r.range(-6.0, 6.0));
        let delta = Vec3::new(r.axis(4.0), r.axis(1.0), r.axis(4.0));
        s.push_str(&slide_row(w, body, feet, delta));
    }
    s
}

fn axis_row(w: &dyn World, body: Body, feet: Vec3, axis: usize, d: f32) -> String {
    let mut out = feet;
    let h = move_axis(w, body, &mut out, axis, d);
    format!("{},{},{},{},{},{},{},{}\n", body.half_width, body.height, f(feet), axis, d, f(out), h.blocked as u8, f(h.normal))
}

pub fn move_axis_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,axis,d,ox,oy,oz,blocked,nx,ny,nz\n");
    for body in BODIES {
        for &a in anchors {
            for axis in 0..3 {
                for d in [-1.0f32, -0.2, 0.0, 0.2, 1.0] {
                    s.push_str(&axis_row(w, body, a, axis, d));
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1500 {
        let body = BODIES[(r.next() * 2.0) as usize % 2];
        let feet = Vec3::new(r.range(-6.0, 6.0), r.range(0.0, 3.0), r.range(-6.0, 6.0));
        let axis = (r.next() * 3.0) as usize % 3;
        let d = r.range(-3.0, 3.0);
        s.push_str(&axis_row(w, body, feet, axis, d));
    }
    s
}
```

In `tools/golden/src/main.rs`, add `mod slides;` after `mod scalars;`, and before `common::write_reference(&dir);` add:

```rust
    let anchors = sweeps::anchors();
    common::write(&dir, "slides.csv", slides::slides_csv(&boxes, &anchors, 0x511D));
    common::write(&dir, "move_axis.csv", slides::move_axis_csv(&boxes, &anchors, 0xA815));
```

Run: `cargo run --release` in `tools/golden`
Expected: it also prints `wrote slides.csv` and `wrote move_axis.csv`. `slides.csv` has over 3000 rows, and `move_axis.csv` over 1500. `git diff --exit-code -- RunlineCore/tests/golden/sweeps.csv` exits 0.

- [ ] **Step 8: Write the golden replay, then run it**

Add `#include "runline/Move.h"` to `golden_tests.cpp`. After the `using namespace runline;` line, add:

```cpp
namespace {
void CheckSlides(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 21);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        Vec3 feet = golden::V(r, 2);
        const auto ns = SlideMove(w, Body{r[0], r[1]}, feet, golden::V(r, 5));
        rep.Vec(i, "feet", r, 8, feet);
        if (ns.size() != static_cast<size_t>(r[11])) {
            rep.Add(i, "count: want " + std::to_string(static_cast<int>(r[11])) + ", got " + std::to_string(ns.size()));
            continue;
        }
        for (size_t k = 0; k < ns.size(); ++k) rep.Vec(i, "n" + std::to_string(k + 1), r, 12 + 3 * k, ns[k]);
    }
    rep.Finish();
}

void CheckMoveAxis(const World& w, const char* file) {
    const auto rows = golden::Require(file, 500, 14);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        Vec3 feet = golden::V(r, 2);
        const AxisHit h = MoveAxis(w, Body{r[0], r[1]}, feet, static_cast<int>(r[5]), r[6]);
        rep.Vec(i, "feet", r, 7, feet);
        rep.Flag(i, "blocked", r[10], h.blocked);
        rep.Vec(i, "normal", r, 11, h.normal);
    }
    rep.Finish();
}
} // namespace

TEST_CASE("golden: SlideMove matches faith_move (boxes)") { CheckSlides(golden::BoxScene(), "slides.csv"); }
TEST_CASE("golden: MoveAxis matches faith_move (boxes)") { CheckMoveAxis(golden::BoxScene(), "move_axis.csv"); }
```

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 27 test cases.

Mutation check (revert afterwards): in `Move.cpp`, change `if (into < 0.0f) rem -= n * into;` to `rem -= n * into;`. Expected: `golden: SlideMove` FAILS, naming `feet.*` fields. Revert, re-run: SUCCESS.

- [ ] **Step 9: Commit**

```bash
git add RunlineCore tools/golden/src
git commit -m "M1a: port move_axis and slide_move with golden parity

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The wall, ledge and ground probes

**Files:**
- Modify: `RunlineCore/CMakeLists.txt`, `RunlineCore/tests/golden_tests.cpp`, `tools/golden/src/main.rs`
- Create: `RunlineCore/include/runline/Probes.h`, `RunlineCore/src/Probes.cpp`, `RunlineCore/tests/probes_tests.cpp`, `tools/golden/src/probes.rs`, `RunlineCore/tests/golden/{probe_wall,trace_wall,trace_box,tops_below,find_ledge}.csv` (generated)

**Interfaces:**
- Consumes: `Body`, `World`, `Aabb`, `FMax`, `FClamp` (Tasks 1–2).
- Produces:
  - `struct Closest { Vec3 point; float along; }` and `Closest ClosestOnSegment(Vec3 a, Vec3 b, Vec3 p)`
  - `struct WallProbe { Vec3 n; float gap; }` and `std::optional<WallProbe> ProbeWall(const World&, Body, Vec3 feet, Vec3 dir, float reach, float yLo, float yHi)`
  - `struct BoxTrace { Vec3 at; Vec3 normal; }` and `std::optional<BoxTrace> TraceBox(const World&, Vec3 start, Vec3 dir, float reach, Vec3 half)`
  - `struct WallHit { Vec3 n; Vec3 at; float gap; }` and `std::optional<WallHit> TraceWall(const World&, Body, Vec3 feet, Vec3 dir, float reach, float y)`
  - `std::vector<float> TopsBelow(const World&, Vec3 p, float halfXZ, float yHi, float yLo)`
  - `float ColumnTop(const World&, Vec3 p, float y0, float y1)`
  - `std::optional<float> FindLedge(const World&, Body, Vec3 feet, Vec3 n, float lo, float hi, float crouchHeight)`
  - In Rust: `probes::{probe_wall_csv, trace_wall_csv, trace_box_csv, tops_below_csv, find_ledge_csv}`, each taking `(w: &dyn World, anchors: &[Vec3], seed: u64)`, except `tops_below_csv(w, anchors)`.

- [ ] **Step 1: Write the failing tests, `RunlineCore/tests/probes_tests.cpp`**

```cpp
#include <doctest/doctest.h>
#include "runline/BoxWorld.h"
#include "runline/Probes.h"
using namespace runline;

namespace {
const Body kStand{0.3f, 1.8f};
// Floor top at y = 0; a wall from x = 2; a 1.2 m block at x -4..-2, z 2..4.
BoxWorld Scene() {
    BoxWorld w;
    w.Add(Aabb::New({-10, -1, -10}, {10, 0, 10}))
     .Add(Aabb::New({2, 0, -10}, {3, 3, 10}))
     .Add(Aabb::New({-4, 0, 2}, {-2, 1.2f, 4}));
    return w;
}
} // namespace

TEST_CASE("ClosestOnSegment clamps to the ends and survives a zero-length segment") {
    const auto c = ClosestOnSegment({0, 0, 0}, {10, 0, 0}, {3, 5, 0});
    CHECK(c.point == Vec3{3, 0, 0});
    CHECK(c.along == doctest::Approx(3.0f));
    CHECK(ClosestOnSegment({0, 0, 0}, {10, 0, 0}, {-2, 1, 0}).along == 0.0f);
    CHECK(ClosestOnSegment({1, 1, 1}, {1, 1, 1}, {5, 5, 5}).point == Vec3{1, 1, 1});
}

TEST_CASE("ProbeWall finds the wall ahead and the gap to it") {
    const auto w = Scene();
    const auto p = ProbeWall(w, kStand, {1.5f, 0, 0}, {1, 0, 0}, 1.0f, 0.0f, 1.8f);
    REQUIRE(p);
    CHECK(p->n == Vec3{-1, 0, 0});
    CHECK(p->gap == doctest::Approx(0.2f));
    CHECK_FALSE(ProbeWall(w, kStand, {1.5f, 0, 0}, {-1, 0, 0}, 1.0f, 0.0f, 1.8f));   // nothing behind
}

TEST_CASE("probes with no direction or no reach find nothing") {                     // Review Focus 5
    const auto w = Scene();
    CHECK_FALSE(ProbeWall(w, kStand, {1.5f, 0, 0}, {0, 1, 0}, 1.0f, 0.0f, 1.8f));      // straight up: no horizontal dir
    CHECK_FALSE(TraceWall(w, kStand, {1.5f, 0, 0}, {0, 0, 0}, 1.0f, 1.0f));
    CHECK_FALSE(TraceWall(w, kStand, {1.5f, 0, 0}, {1, 0, 0}, 0.0f, 1.0f));
    CHECK_FALSE(TraceBox(w, {0, 1, 0}, {0, 0, 0}, 1.0f, Vec3::Splat(0.05f)));
    CHECK_FALSE(TraceBox(w, {0, 1, 0}, {1, 0, 0}, 0.0f, Vec3::Splat(0.05f)));
}

TEST_CASE("TraceWall gives the face point straight out from the body") {
    const auto w = Scene();
    const auto h = TraceWall(w, kStand, {1.5f, 0, 0}, {1, 0, 0}, 1.0f, 1.0f);
    REQUIRE(h);
    CHECK(h->n == Vec3{-1, 0, 0});
    CHECK(h->at.x == doctest::Approx(2.0f));
    CHECK(h->at.y == doctest::Approx(1.0f));
    CHECK(h->gap == doctest::Approx(0.2f));
}

TEST_CASE("TopsBelow lists every walkable top under a column, highest first") {
    const auto w = Scene();
    const auto tops = TopsBelow(w, {-3, 0, 3}, 0.01f, 7.0f, -1.0f);
    REQUIRE(tops.size() == 2);
    CHECK(tops[0] == doctest::Approx(1.2f));
    CHECK(tops[1] == doctest::Approx(0.0f));
    CHECK(TopsBelow(w, {-3, 0, 3}, 0.01f, 0.5f, 2.0f).empty());   // y_lo above y_hi: nothing
    CHECK(ColumnTop(w, {0, 0, 0}, -1.0f, 5.0f) == doctest::Approx(0.0f));
    CHECK(ColumnTop(w, {20, 0, 0}, -1.0f, 5.0f) == -1.0f);        // nothing there: y0
}

TEST_CASE("FindLedge finds a climbable top, and refuses one with no room for the hands") {
    auto w = Scene();
    const auto top = FindLedge(w, kStand, {-1.7f, 0, 3}, {1, 0, 0}, 0.5f, 2.5f, 1.22f);
    REQUIRE(top);
    CHECK(*top == doctest::Approx(1.2f));
    w.Add(Aabb::New({-2.2f, 1.3f, 2}, {-1.3f, 1.5f, 4}));          // overhang where the hands go
    CHECK_FALSE(FindLedge(w, kStand, {-1.7f, 0, 3}, {1, 0, 0}, 0.5f, 2.5f, 1.22f));
}
```

In `RunlineCore/CMakeLists.txt`, add `src/Probes.cpp` to the library and `tests/probes_tests.cpp` to the tests. Create a placeholder `RunlineCore/src/Probes.cpp` containing `#include "runline/Probes.h"`, and a placeholder `RunlineCore/include/runline/Probes.h` containing `#pragma once`.

- [ ] **Step 2: Run them and check they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: build errors, `ClosestOnSegment` and `ProbeWall` undeclared.

- [ ] **Step 3: Write `RunlineCore/include/runline/Probes.h`**

```cpp
#pragma once
#include <optional>
#include <vector>
#include "runline/Move.h"

namespace runline {

/// Closest point on segment a–b to p, and its distance along from a.
struct Closest { Vec3 point; float along; };
Closest ClosestOnSegment(Vec3 a, Vec3 b, Vec3 p);

/// A wall face in front of the body: its horizontal normal and the gap to it.
struct WallProbe { Vec3 n; float gap; };
/// Sweep a slice of the body (from `yLo` to `yHi` above the feet) along `dir` for `reach`;
/// the wall it meets, if it faces us within about 80 degrees.
std::optional<WallProbe> ProbeWall(const World& world, Body body, Vec3 feet, Vec3 dir, float reach, float yLo, float yHi);

/// Where a small box swept from `start` along `dir` stopped, and the surface's normal.
struct BoxTrace { Vec3 at; Vec3 normal; };
std::optional<BoxTrace> TraceBox(const World& world, Vec3 start, Vec3 dir, float reach, Vec3 half);

/// A wall face found by TraceWall.
struct WallHit {
    /// Outward face normal (horizontal).
    Vec3 n;
    /// Where the body's centre line, straight along the normal, meets the face (at the trace height).
    Vec3 at;
    /// Gap between the body and the face.
    float gap;
};
/// Sweep a thin slice of the body (`y` above the feet, ±2 cm like ME's extent traces)
/// horizontally along `dir` for up to `reach`: the first wall it runs into, at any angle.
std::optional<WallHit> TraceWall(const World& world, Body body, Vec3 feet, Vec3 dir, float reach, float y);

/// Heights of the walkable tops under a column (half size `halfXZ` across) at `p`, highest
/// first, from `yHi` down to `yLo`.
std::vector<float> TopsBelow(const World& world, Vec3 p, float halfXZ, float yHi, float yLo);

/// Height of the top of whatever stands at `p` (x, z), traced down from `y1`; `y0` if nothing.
float ColumnTop(const World& world, Vec3 p, float y0, float y1);

/// A climbable top edge on the wall with outward normal `n`, between `lo` and `hi` above the
/// feet, with room to crouch on top and nothing overhanging the hands (TdPawn's FindLedge).
std::optional<float> FindLedge(const World& world, Body body, Vec3 feet, Vec3 n, float lo, float hi, float crouchHeight);

} // namespace runline
```

- [ ] **Step 4: Write `RunlineCore/src/Probes.cpp`, a line-for-line port**

```cpp
#include "runline/Probes.h"
#include <algorithm>
#include <cmath>

namespace runline {
namespace {

/// The horizontal part of a surface normal, if it's a wall (steeper than ~45 degrees).
std::optional<Vec3> WallNormal(Vec3 n) {
    const Vec3 h{n.x, 0.0f, n.z};
    if (std::fabs(n.y) < 0.7f && h.LengthSquared() > 1e-6f) return h.Normalize();
    return std::nullopt;
}

/// Gap between the body and the plane through `point` with horizontal normal `n`.
float WallGap(Body body, Vec3 feet, Vec3 n, Vec3 point) {
    const float d = Vec3{feet.x - point.x, 0.0f, feet.z - point.z}.Dot(n);
    return FMax(d - body.ReachToward(n), 0.0f);
}

} // namespace

Closest ClosestOnSegment(Vec3 a, Vec3 b, Vec3 p) {
    const Vec3 ab = b - a;
    const float len = ab.Length();
    if (len < 1e-6f) return {a, 0.0f};
    const Vec3 u = ab / len;
    const float s = FClamp((p - a).Dot(u), 0.0f, len);
    return {a + u * s, s};
}

std::optional<WallProbe> ProbeWall(const World& world, Body body, Vec3 feet, Vec3 dir, float reach, float yLo, float yHi) {
    dir = Vec3{dir.x, 0.0f, dir.z}.NormalizeOrZero();
    if (dir == Vec3::Zero()) return std::nullopt;
    Aabb me = body.AabbAt(feet);
    me.min.y = feet.y + yLo;
    me.max.y = feet.y + yHi;
    const auto h = world.Sweep(me.Size() * 0.5f, me.Center(), dir * reach);
    if (!h) return std::nullopt;
    const auto n = WallNormal(h->normal);
    if (!n) return std::nullopt;
    if (n->Dot(dir) > -0.2f) return std::nullopt;
    return WallProbe{*n, WallGap(body, feet, *n, h->point)};
}

std::optional<BoxTrace> TraceBox(const World& world, Vec3 start, Vec3 dir, float reach, Vec3 half) {
    dir = dir.NormalizeOrZero();
    if (dir == Vec3::Zero() || reach <= 0.0f) return std::nullopt;
    const auto h = world.Sweep(half, start, dir * reach);
    if (!h) return std::nullopt;
    return BoxTrace{start + dir * reach * h->t, h->normal};
}

std::optional<WallHit> TraceWall(const World& world, Body body, Vec3 feet, Vec3 dir, float reach, float y) {
    dir = Vec3{dir.x, 0.0f, dir.z}.NormalizeOrZero();
    if (dir == Vec3::Zero() || reach <= 0.0f) return std::nullopt;
    const Vec3 half{body.halfWidth, 0.02f, body.halfWidth};
    const Vec3 c = feet + Vec3::UnitY() * y;
    const auto h = world.Sweep(half, c, dir * reach);
    if (!h) return std::nullopt;
    const auto n = WallNormal(h->normal);
    if (!n) return std::nullopt;
    const Vec3 p{c.x, c.y, c.z};
    const Vec3 at = p - *n * Vec3{p.x - h->point.x, 0.0f, p.z - h->point.z}.Dot(*n);
    return WallHit{*n, at, WallGap(body, feet, *n, h->point)};
}

std::vector<float> TopsBelow(const World& world, Vec3 p, float halfXZ, float yHi, float yLo) {
    const Vec3 half{halfXZ, 0.005f, halfXZ};
    std::vector<float> out;
    float y = yHi;
    for (int i = 0; i < 8; ++i) {
        const float d = yLo - y;
        if (d >= 0.0f) break;
        const Vec3 c{p.x, y + half.y, p.z};
        const auto h = world.Sweep(half, c, Vec3::UnitY() * d);
        if (!h) break;
        const float top = y + d * h->t;
        if (h->normal.y > 0.7f) out.push_back(top);
        y = top - 0.03f;
    }
    return out;
}

float ColumnTop(const World& world, Vec3 p, float y0, float y1) {
    const auto tops = TopsBelow(world, p, 0.01f, y1, y0);
    return tops.empty() ? y0 : tops.front();
}

std::optional<float> FindLedge(const World& world, Body body, Vec3 feet, Vec3 n, float lo, float hi, float crouchHeight) {
    const float hw = body.halfWidth;
    n = Vec3{n.x, 0.0f, n.z}.NormalizeOrZero();
    // Column just inside the wall face, in front of the player.
    const Vec3 inside = feet - n * (body.ReachToward(n) + 0.02f + hw);
    const Aabb me = body.AabbAt(feet);
    auto tops = TopsBelow(world, inside, hw, feet.y + hi + 0.05f, feet.y + lo - 0.05f);
    std::erase_if(tops, [&](float t) { return !(t >= feet.y + lo && t <= feet.y + hi); });
    std::stable_sort(tops.begin(), tops.end());   // Rust's sort_by is stable
    for (const float t : tops) {
        // Room to crouch on top of the ledge.
        const Aabb stand = Aabb::New({inside.x - hw, t + 0.01f, inside.z - hw}, {inside.x + hw, t + crouchHeight, inside.z + hw});
        if (!world.IsFree(stand)) continue;
        // Hands/head space on our side of the wall, just above the lip.
        Aabb hands = me;
        hands.min.y = FMax(t, feet.y + 0.1f);
        hands.max.y = t + 0.35f;
        if (!world.IsFree(hands)) continue;
        return t;
    }
    return std::nullopt;
}

} // namespace runline
```

- [ ] **Step 5: Run and check it passes**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 33 test cases.

- [ ] **Step 6: Generate the probe goldens**

Create `tools/golden/src/probes.rs`:

```rust
//! The wall, ledge and ground probes every move builds on.
use crate::common::{f, Lcg};
use crate::slides::{dirs8, BODIES};
use faith_move::world::{find_ledge, probe_wall, tops_below, trace_box, trace_wall, Body, World};
use glam::Vec3;

fn rand_feet(r: &mut Lcg) -> Vec3 {
    let y = if r.next() < 0.7 { 0.0 } else { r.range(0.0, 3.0) };
    Vec3::new(r.range(-6.0, 6.0), y, r.range(-6.0, 6.0))
}
fn rand_dir(r: &mut Lcg) -> Vec3 {
    let a = r.range(0.0, std::f32::consts::TAU);
    Vec3::new(a.cos(), 0.0, a.sin())
}
fn rand_body(r: &mut Lcg) -> Body { BODIES[(r.next() * 2.0) as usize % 2] }

fn probe_wall_row(w: &dyn World, body: Body, feet: Vec3, dir: Vec3, reach: f32, lo: f32, hi: f32) -> String {
    let (found, n, gap) = match probe_wall(w, body, feet, dir, reach, lo, hi) {
        Some((n, gap)) => (1, n, gap),
        None => (0, Vec3::ZERO, 0.0),
    };
    format!("{},{},{},{},{},{},{},{},{},{}\n", body.half_width, body.height, f(feet), f(dir), reach, lo, hi, found, f(n), gap)
}

pub fn probe_wall_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,dx,dy,dz,reach,ylo,yhi,found,nx,ny,nz,gap\n");
    let mut ds = dirs8();
    ds.push(Vec3::new(1.0, 0.5, 0.0));
    ds.push(Vec3::ZERO);
    for body in BODIES {
        for &a in anchors {
            for &d in &ds {
                for reach in [0.5f32, 2.0] {
                    for (lo, hi) in [(0.0f32, 1.8f32), (1.0, 1.6), (0.3, 0.6)] {
                        s.push_str(&probe_wall_row(w, body, a, d, reach, lo, hi));
                    }
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1500 {
        let (body, feet, d) = (rand_body(&mut r), rand_feet(&mut r), rand_dir(&mut r));
        let reach = r.range(0.1, 3.0);
        let lo = r.range(0.0, 1.0);
        let hi = lo + r.range(0.1, 1.0);
        s.push_str(&probe_wall_row(w, body, feet, d, reach, lo, hi));
    }
    s
}

fn trace_wall_row(w: &dyn World, body: Body, feet: Vec3, dir: Vec3, reach: f32, y: f32) -> String {
    let (found, n, at, gap) = match trace_wall(w, body, feet, dir, reach, y) {
        Some(h) => (1, h.n, h.at, h.gap),
        None => (0, Vec3::ZERO, Vec3::ZERO, 0.0),
    };
    format!("{},{},{},{},{},{},{},{},{},{}\n", body.half_width, body.height, f(feet), f(dir), reach, y, found, f(n), f(at), gap)
}

pub fn trace_wall_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,dx,dy,dz,reach,y,found,nx,ny,nz,ax,ay,az,gap\n");
    let mut ds = dirs8();
    ds.push(Vec3::ZERO);
    for body in BODIES {
        for &a in anchors {
            for &d in &ds {
                for reach in [0.0f32, 0.5, 2.0] {
                    for y in [0.3f32, 1.0, 1.7] {
                        s.push_str(&trace_wall_row(w, body, a, d, reach, y));
                    }
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1500 {
        let (body, feet, d) = (rand_body(&mut r), rand_feet(&mut r), rand_dir(&mut r));
        let reach = r.range(0.1, 3.0);
        let y = r.range(0.0, 1.8);
        s.push_str(&trace_wall_row(w, body, feet, d, reach, y));
    }
    s
}

fn trace_box_row(w: &dyn World, start: Vec3, dir: Vec3, reach: f32, half: Vec3) -> String {
    let (found, at, n) = match trace_box(w, start, dir, reach, half) {
        Some((at, n)) => (1, at, n),
        None => (0, Vec3::ZERO, Vec3::ZERO),
    };
    format!("{},{},{},{},{},{},{}\n", f(start), f(dir), reach, f(half), found, f(at), f(n))
}

pub fn trace_box_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("sx,sy,sz,dx,dy,dz,reach,hx,hy,hz,found,px,py,pz,nx,ny,nz\n");
    let mut ds = dirs8();
    ds.extend([Vec3::new(0.0, -1.0, 0.0), Vec3::new(0.0, 1.0, 0.0), Vec3::ZERO]);
    for &a in anchors {
        for &d in &ds {
            for reach in [0.0f32, 1.0, 4.0] {
                for half in [Vec3::splat(0.05), Vec3::new(0.3, 0.02, 0.3)] {
                    s.push_str(&trace_box_row(w, a + Vec3::Y, d, reach, half));
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1000 {
        let start = rand_feet(&mut r) + Vec3::Y * r.range(0.0, 2.0);
        let dir = Vec3::new(r.range(-1.0, 1.0), r.range(-1.0, 1.0), r.range(-1.0, 1.0));
        let reach = r.range(0.0, 5.0);
        let half = Vec3::splat(r.range(0.01, 0.3));
        s.push_str(&trace_box_row(w, start, dir, reach, half));
    }
    s
}

fn tops_row(w: &dyn World, p: Vec3, hxz: f32, y_hi: f32, y_lo: f32) -> String {
    let ts = tops_below(w, p, hxz, y_hi, y_lo);
    let mut s = format!("{},{},{},{},{}", f(p), hxz, y_hi, y_lo, ts.len());
    for i in 0..8 {
        s.push_str(&format!(",{}", ts.get(i).copied().unwrap_or(0.0)));
    }
    s + "\n"
}

/// Every anchor's column and a 1 m grid over the scene, two widths, three height ranges
/// (the last one empty: y_lo above y_hi).
pub fn tops_below_csv(w: &dyn World, anchors: &[Vec3]) -> String {
    let mut s = String::from("px,py,pz,hxz,yhi,ylo,count,t1,t2,t3,t4,t5,t6,t7,t8\n");
    let mut pts: Vec<Vec3> = anchors.iter().map(|a| Vec3::new(a.x, 0.0, a.z)).collect();
    for i in -6..=6 {
        for j in -6..=6 {
            pts.push(Vec3::new(i as f32, 0.0, j as f32));
        }
    }
    for p in pts {
        for hxz in [0.01f32, 0.3] {
            for (hi, lo) in [(7.0f32, -1.0f32), (2.0, 0.5), (0.5, 2.0)] {
                s.push_str(&tops_row(w, p, hxz, hi, lo));
            }
        }
    }
    s
}

fn ledge_row(w: &dyn World, body: Body, feet: Vec3, n: Vec3, lo: f32, hi: f32, crouch: f32) -> String {
    let (found, top) = match find_ledge(w, body, feet, n, lo, hi, crouch) {
        Some(t) => (1, t),
        None => (0, 0.0),
    };
    format!("{},{},{},{},{},{},{},{},{}\n", body.half_width, body.height, f(feet), f(n), lo, hi, crouch, found, top)
}

pub fn find_ledge_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,nx,ny,nz,lo,hi,crouch,found,top\n");
    let normals = [Vec3::X, -Vec3::X, Vec3::Z, -Vec3::Z, Vec3::new(1.0, 0.0, 1.0), Vec3::new(-1.0, 0.0, -1.0), Vec3::ZERO];
    for body in BODIES {
        for &a in anchors {
            for n in normals {
                for (lo, hi) in [(0.5f32, 2.5f32), (1.0, 1.6), (0.0, 6.5)] {
                    s.push_str(&ledge_row(w, body, a, n, lo, hi, 1.22));
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1500 {
        let (body, feet, n) = (rand_body(&mut r), rand_feet(&mut r), rand_dir(&mut r));
        let lo = r.range(0.0, 2.0);
        let hi = lo + r.range(0.2, 4.0);
        s.push_str(&ledge_row(w, body, feet, n, lo, hi, 1.22));
    }
    s
}
```

In `main.rs`, add `mod probes;`, and before `common::write_reference(&dir);` add:

```rust
    common::write(&dir, "probe_wall.csv", probes::probe_wall_csv(&boxes, &anchors, 0x9B0E));
    common::write(&dir, "trace_wall.csv", probes::trace_wall_csv(&boxes, &anchors, 0x7A11));
    common::write(&dir, "trace_box.csv", probes::trace_box_csv(&boxes, &anchors, 0x7B0C));
    common::write(&dir, "tops_below.csv", probes::tops_below_csv(&boxes, &anchors));
    common::write(&dir, "find_ledge.csv", probes::find_ledge_csv(&boxes, &anchors, 0x1ED6));
```

Run: `cargo run --release` in `tools/golden`
Expected: it prints all five new files. Earlier files are unchanged: `git diff --exit-code -- RunlineCore/tests/golden/sweeps.csv RunlineCore/tests/golden/slides.csv` exits 0.

- [ ] **Step 7: Write the golden replays, then run them**

Add `#include "runline/Probes.h"` to `golden_tests.cpp`, and add these to its anonymous namespace:

```cpp
void CheckProbeWall(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 16);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto p = ProbeWall(w, Body{r[0], r[1]}, golden::V(r, 2), golden::V(r, 5), r[8], r[9], r[10]);
        rep.Flag(i, "found", r[11], p.has_value());
        if (!p || r[11] == 0.0f) continue;
        rep.Vec(i, "n", r, 12, p->n);
        rep.Float(i, "gap", r[15], p->gap);
    }
    rep.Finish();
}

void CheckTraceWall(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 18);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto h = TraceWall(w, Body{r[0], r[1]}, golden::V(r, 2), golden::V(r, 5), r[8], r[9]);
        rep.Flag(i, "found", r[10], h.has_value());
        if (!h || r[10] == 0.0f) continue;
        rep.Vec(i, "n", r, 11, h->n);
        rep.Vec(i, "at", r, 14, h->at);
        rep.Float(i, "gap", r[17], h->gap);
    }
    rep.Finish();
}

void CheckTraceBox(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 17);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto h = TraceBox(w, golden::V(r, 0), golden::V(r, 3), r[6], golden::V(r, 7));
        rep.Flag(i, "found", r[10], h.has_value());
        if (!h || r[10] == 0.0f) continue;
        rep.Vec(i, "at", r, 11, h->at);
        rep.Vec(i, "normal", r, 14, h->normal);
    }
    rep.Finish();
}

void CheckTopsBelow(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 15);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto ts = TopsBelow(w, golden::V(r, 0), r[3], r[4], r[5]);
        if (ts.size() != static_cast<size_t>(r[6])) {
            rep.Add(i, "count: want " + std::to_string(static_cast<int>(r[6])) + ", got " + std::to_string(ts.size()));
            continue;
        }
        for (size_t k = 0; k < ts.size(); ++k) rep.Float(i, "t" + std::to_string(k + 1), r[7 + k], ts[k]);
    }
    rep.Finish();
}

void CheckFindLedge(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 13);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto top = FindLedge(w, Body{r[0], r[1]}, golden::V(r, 2), golden::V(r, 5), r[8], r[9], r[10]);
        rep.Flag(i, "found", r[11], top.has_value());
        if (top && r[11] != 0.0f) rep.Float(i, "top", r[12], *top);
    }
    rep.Finish();
}
```

and these test cases at the end of the file:

```cpp
TEST_CASE("golden: ProbeWall matches faith_move (boxes)") { CheckProbeWall(golden::BoxScene(), "probe_wall.csv"); }
TEST_CASE("golden: TraceWall matches faith_move (boxes)") { CheckTraceWall(golden::BoxScene(), "trace_wall.csv"); }
TEST_CASE("golden: TraceBox matches faith_move (boxes)") { CheckTraceBox(golden::BoxScene(), "trace_box.csv"); }
TEST_CASE("golden: TopsBelow matches faith_move (boxes)") { CheckTopsBelow(golden::BoxScene(), "tops_below.csv"); }
TEST_CASE("golden: FindLedge matches faith_move (boxes)") { CheckFindLedge(golden::BoxScene(), "find_ledge.csv"); }
```

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 38 test cases.

Mutation check (revert afterwards): in `Probes.cpp` `FindLedge`, change `hands.max.y = t + 0.35f;` to `t + 0.25f`. Expected: `golden: FindLedge` FAILS on `found`. Revert, re-run: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add RunlineCore tools/golden/src
git commit -m "M1a: port the wall, ledge and ground probes with golden parity

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: MeshWorld (triangles at any angle)

**Files:**
- Modify: `RunlineCore/CMakeLists.txt`, `RunlineCore/tests/Golden.h`, `RunlineCore/tests/golden_tests.cpp`, `tools/golden/src/main.rs`
- Create: `RunlineCore/include/runline/MeshWorld.h`, `RunlineCore/src/MeshWorld.cpp`, `RunlineCore/tests/mesh_world_tests.cpp`, `tools/golden/src/mesh.rs`, `RunlineCore/tests/golden/mesh_{scene,sweeps,slides,trace_wall,find_ledge}.csv` (generated)

**Interfaces:**
- Consumes: everything from Tasks 1–3, and Rust's `sweeps::row`, `slides::slides_csv` and `probes::{trace_wall_csv, find_ledge_csv}`.
- Produces:
  - `using Tri = std::array<Vec3, 3>;`
  - `Aabb TriBounds(const Tri&)`
  - `bool TriBoxOverlap(const Tri&, Vec3 c, Vec3 h)`
  - `class MeshWorld : public World { std::vector<Tri> tris; static std::array<Tri, 2> Quad(Vec3, Vec3, Vec3, Vec3); ... }`
  - In `Golden.h`: `golden::MeshScene()`

The Rust version's grid index is left out: it only narrows which triangles get checked, in the same index order, so a full scan gives identical results. Fixtures come in M4.

- [ ] **Step 1: Write the failing tests, `RunlineCore/tests/mesh_world_tests.cpp`**

```cpp
#include <doctest/doctest.h>
#include "runline/MeshWorld.h"
using namespace runline;

namespace {
const Vec3 kHalf{0.3f, 0.9f, 0.3f};
MeshWorld Floor() {   // a 20 m floor at y = 0, as two triangles
    MeshWorld w;
    for (const Tri& t : MeshWorld::Quad({-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10})) w.tris.push_back(t);
    return w;
}
} // namespace

TEST_CASE("TriBoxOverlap is strict: resting on a triangle doesn't count") {
    const auto w = Floor();
    CHECK(TriBoxOverlap(w.tris[0], {-5, 0.5f, 5}, kHalf));       // cuts into the floor
    CHECK_FALSE(TriBoxOverlap(w.tris[0], {-5, 0.9f, 5}, kHalf)); // resting exactly on it
    CHECK_FALSE(TriBoxOverlap(w.tris[0], {-5, 3, 5}, kHalf));    // well above
}

TEST_CASE("falling onto a mesh floor stops just above it, facing up") {
    const auto w = Floor();
    const auto h = w.Sweep(kHalf, {0, 2, 0}, {0, -2, 0});
    REQUIRE(h);
    CHECK(h->normal == Vec3{0, 1, 0});                          // flipped to face the box
    CHECK(h->t == doctest::Approx(0.55f).epsilon(1e-3));
    CHECK(h->point.y == doctest::Approx(0.0f).epsilon(1e-3));
}

TEST_CASE("a box sunk less than SKIN is held, but can always get out") {     // Review Focus 4
    const auto w = Floor();
    const Vec3 sunk{0, 0.9f - 0.01f, 0};                        // 1 cm into the floor
    const auto down = w.Sweep(kHalf, sunk, {0, -0.5f, 0});
    REQUIRE(down);
    CHECK(down->t == 0.0f);
    CHECK(down->normal == Vec3{0, 1, 0});
    CHECK_FALSE(w.Sweep(kHalf, sunk, {0, 0.5f, 0}));             // up and out
    CHECK_FALSE(w.Sweep(kHalf, sunk, {1, 0, 0}));                // sideways
}

TEST_CASE("a zero-area triangle doesn't crash or block") {
    MeshWorld w;
    w.tris.push_back(Tri{Vec3{0, 1, 0}, Vec3{0, 1, 0}, Vec3{0, 1, 0}});
    CHECK_FALSE(w.Sweep(kHalf, {0, 1, 0}, {1, 0, 0}));
}

TEST_CASE("MeshWorld::Overlaps") {
    const auto w = Floor();
    CHECK(w.Overlaps(Aabb::New({-1, -0.5f, -1}, {1, 0.5f, 1})));
    CHECK_FALSE(w.Overlaps(Aabb::New({-1, 0, -1}, {1, 1, 1})));  // resting on top
    CHECK(TriBounds(w.tris[0]).min == Vec3{-10, 0, -10});
}
```

In `RunlineCore/CMakeLists.txt`, add `src/MeshWorld.cpp` to the library and `tests/mesh_world_tests.cpp` to the tests. Create a placeholder `RunlineCore/src/MeshWorld.cpp` containing `#include "runline/MeshWorld.h"`, and a placeholder `RunlineCore/include/runline/MeshWorld.h` containing `#pragma once`.

- [ ] **Step 2: Run them and check they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: build errors, `MeshWorld` and `Tri` undeclared.

- [ ] **Step 3: Write `RunlineCore/include/runline/MeshWorld.h`**

```cpp
#pragma once
#include <array>
#include <vector>
#include "runline/World.h"

namespace runline {

using Tri = std::array<Vec3, 3>;

Aabb TriBounds(const Tri& t);

/// Separating-axis test: does the triangle cut into the box (centre `c`, half size `h`)?
/// Akenine-Moller's box / triangle overlap, strict (touching doesn't count).
bool TriBoxOverlap(const Tri& t, Vec3 c, Vec3 h);

/// Triangles at any angle: what a game engine's static geometry is. Sweeps step the box along
/// the path (2 cm) and narrow the first contact down by bisection; overlaps are the
/// separating-axis box / triangle test.
class MeshWorld : public World {
public:
    std::vector<Tri> tris;

    /// Two triangles making the quad `a b c d` (in order round it).
    static std::array<Tri, 2> Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) { return {Tri{a, b, c}, Tri{a, c, d}}; }

    std::optional<SweepHit> Sweep(Vec3 half, Vec3 start, Vec3 delta) const override;
    bool Overlaps(const Aabb& region) const override;
};

} // namespace runline
```

- [ ] **Step 4: Write `RunlineCore/src/MeshWorld.cpp`, a line-for-line port**

```cpp
#include "runline/MeshWorld.h"
#include <cmath>
#include <cstddef>
#include <utility>

namespace runline {
namespace {

float Support(Vec3 half, Vec3 n) { return half.x * std::fabs(n.x) + half.y * std::fabs(n.y) + half.z * std::fabs(n.z); }

Vec3 FaceNormal(const Tri& t) { return (t[1] - t[0]).Cross(t[2] - t[0]).NormalizeOrZero(); }

} // namespace

Aabb TriBounds(const Tri& t) { return Aabb{Vec3::Min(Vec3::Min(t[0], t[1]), t[2]), Vec3::Max(Vec3::Max(t[0], t[1]), t[2])}; }

bool TriBoxOverlap(const Tri& t, Vec3 c, Vec3 h) {
    const Vec3 v[3] = {t[0] - c, t[1] - c, t[2] - c};
    const Vec3 e[3] = {v[1] - v[0], v[2] - v[1], v[0] - v[2]};
    const auto sep = [&](Vec3 axis) {
        if (axis.LengthSquared() < 1e-12f) return false;
        axis = axis.Normalize();
        const float p0 = v[0].Dot(axis), p1 = v[1].Dot(axis), p2 = v[2].Dot(axis);
        const float r = h.x * std::fabs(axis.x) + h.y * std::fabs(axis.y) + h.z * std::fabs(axis.z);
        const float mn = FMin(FMin(p0, p1), p2);
        const float mx = FMax(FMax(p0, p1), p2);
        return mn >= r - TOUCH || mx <= -r + TOUCH;
    };
    const Vec3 axes[3] = {Vec3::UnitX(), Vec3::UnitY(), Vec3::UnitZ()};
    for (const Vec3& a : axes)
        if (sep(a)) return false;
    const Vec3 n = e[0].Cross(e[1]);
    if (sep(n)) return false;
    for (const Vec3& a : axes)
        for (const Vec3& ed : e)
            if (sep(a.Cross(ed))) return false;
    return true;
}

std::optional<SweepHit> MeshWorld::Sweep(Vec3 half, Vec3 start, Vec3 delta) const {
    const Aabb a = Aabb::New(start - half, start + half);
    const Aabb region = a.Union(a.Translated(delta));
    const Aabb grown = Aabb::New(region.min - Vec3::Splat(0.01f), region.max + Vec3::Splat(0.01f));
    std::vector<const Tri*> near;
    for (const Tri& t : tris)
        if (TriBounds(t).Overlaps(grown)) near.push_back(&t);
    // A surface the box already cuts into: what it starts inside doesn't count (it can always
    // get out), except a surface it has only sunk into by a hair (rounding far from the origin,
    // a refreshed world): moving further into that is blocked straight away, so the box can't
    // sink on through it.
    std::optional<std::pair<float, Vec3>> skin;
    for (const Tri* t : near) {
        if (!TriBoxOverlap(*t, start, half)) continue;
        Vec3 n = FaceNormal(*t);
        if (n == Vec3::Zero()) continue;
        if (n.Dot(start - (*t)[0]) < 0.0f) n = -n;
        const float depth = Support(half, n) - n.Dot(start - (*t)[0]);
        const float toward = -n.Dot(delta);
        if (depth < SKIN && toward > 1e-7f && (!skin || toward > skin->first)) skin = std::pair{toward, n};
    }
    if (skin) {
        const Vec3 n = skin->second;
        return SweepHit{0.0f, n, start - n * Support(half, n)};
    }
    std::vector<const Tri*> ts;
    for (const Tri* t : near)
        if (!TriBoxOverlap(*t, start, half)) ts.push_back(t);
    if (ts.empty()) return std::nullopt;
    const auto hitAt = [&](float s) {
        for (const Tri* t : ts)
            if (TriBoxOverlap(*t, start + delta * s, half)) return true;
        return false;
    };
    const float len = delta.Length();
    // Steps no longer than the box is thick along the path, so a thin box can't skip over a
    // surface between two of them.
    const Vec3 dir = delta / FMax(len, 1e-9f);
    const float thick = 2.0f * (half.x * std::fabs(dir.x) + half.y * std::fabs(dir.y) + half.z * std::fabs(dir.z));
    const float step = FMax(FMin(0.02f, thick * 0.9f), 1e-4f);
    const std::size_t steps = static_cast<std::size_t>(FMax(std::ceil(len / step), 1.0f));
    float prev = 0.0f;
    for (std::size_t i = 1; i <= steps; ++i) {
        const float s = static_cast<float>(i) / static_cast<float>(steps);
        if (hitAt(s)) {
            float lo = prev, hi = s;
            for (int k = 0; k < 14; ++k) {
                const float mid = (lo + hi) * 0.5f;
                if (hitAt(mid)) hi = mid;
                else lo = mid;
            }
            // Of the triangles touched (several at an edge), the one met most head-on.
            const Vec3 at = start + delta * lo;
            // Surfaces met within a hair of each other count as met together (the bisection
            // settles on whichever overlap test flips first, to rounding).
            const Vec3 probe = start + delta * FMin(hi + 4.0f * TOUCH / FMax(len, 1e-9f), 1.0f);
            std::optional<std::pair<float, Vec3>> best;
            for (const Tri* t : ts) {
                if (!TriBoxOverlap(*t, probe, half)) continue;
                Vec3 n = FaceNormal(*t);
                if (n.Dot(at - (*t)[0]) < 0.0f) n = -n;
                const float headOn = -n.Dot(delta);
                if (!best || headOn > best->first) best = std::pair{headOn, n};
            }
            if (!best) return std::nullopt;
            const Vec3 n = best->second;
            // Bisection stops where the overlap test flips: TOUCH deep. Back off along the path
            // to just outside the surface, so sliding along it from here is clear.
            const float toward = -n.Dot(delta);
            const float t = toward > 1e-6f ? FMax(lo - 2.0f * TOUCH / toward, 0.0f) : lo;
            const Vec3 at2 = start + delta * t;
            return SweepHit{t, n, at2 - n * Support(half, n)};
        }
        prev = s;
    }
    return std::nullopt;
}

bool MeshWorld::Overlaps(const Aabb& region) const {
    const Vec3 c = region.Center();
    const Vec3 h = region.Size() * 0.5f;
    for (const Tri& t : tris)
        if (TriBounds(t).Overlaps(region) && TriBoxOverlap(t, c, h)) return true;
    return false;
}

} // namespace runline
```

- [ ] **Step 5: Run and check it passes**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 43 test cases.

- [ ] **Step 6: Generate the mesh goldens**

Create `tools/golden/src/mesh.rs`:

```rust
//! A triangle scene: a wall and a block turned off the grid, a walkable ramp and a steep one.
use crate::common::{f, Lcg};
use crate::{probes, slides, sweeps};
use faith_move::world::MeshWorld;
use glam::Vec3;
use std::fmt::Write as _;

pub fn scene() -> MeshWorld {
    let v = Vec3::new;
    let mut t: Vec<[Vec3; 3]> = vec![];
    t.extend(MeshWorld::quad(v(-12.0, 0.0, -12.0), v(-12.0, 0.0, 12.0), v(12.0, 0.0, 12.0), v(12.0, 0.0, -12.0)));
    t.extend(MeshWorld::oriented_box(v(3.0, 1.5, 0.0), v(0.5, 1.5, 2.0), 0.4712)); // wall turned 27 degrees
    t.extend(MeshWorld::oriented_box(v(-3.0, 0.6, 3.0), v(1.0, 0.6, 1.0), -0.3)); // waist-high block
    t.extend(MeshWorld::quad(v(4.0, 0.0, 4.0), v(8.0, 2.0, 4.0), v(8.0, 2.0, 7.0), v(4.0, 0.0, 7.0))); // walkable ramp
    t.extend(MeshWorld::quad(v(-8.0, 0.0, -2.0), v(-7.0, 1.732, -2.0), v(-7.0, 1.732, 2.0), v(-8.0, 0.0, 2.0))); // 60-degree ramp
    MeshWorld::new(t, vec![])
}

pub fn scene_csv(w: &MeshWorld) -> String {
    let mut s = String::from("ax,ay,az,bx,by,bz,cx,cy,cz\n");
    for t in &w.tris {
        writeln!(s, "{},{},{}", f(t[0]), f(t[1]), f(t[2])).unwrap();
    }
    s
}

pub fn anchors() -> Vec<Vec3> {
    vec![
        Vec3::new(2.0, 0.0, 0.0),   // in front of the turned wall
        Vec3::new(3.5, 0.0, -2.2),  // by its far corner
        Vec3::new(-3.0, 0.0, 1.5),  // in front of the turned block
        Vec3::new(-3.0, 1.2, 3.0),  // on the block
        Vec3::new(3.6, 0.0, 5.5),   // at the foot of the ramp
        Vec3::new(6.0, 1.0, 5.5),   // on the ramp
        Vec3::new(-6.6, 0.0, 0.0),  // at the foot of the steep ramp
    ]
}

pub fn sweeps_csv(w: &MeshWorld) -> String {
    let mut s = String::from("hx,hy,hz,sx,sy,sz,dx,dy,dz,hit,t,nx,ny,nz,px,py,pz\n");
    let halves = [Vec3::new(0.3, 0.9, 0.3), Vec3::new(0.3, 0.61, 0.3), Vec3::splat(0.05)];
    let mut ds = slides::dirs8();
    ds.extend([Vec3::new(0.0, -1.0, 0.0), Vec3::new(1.0, -0.5, 0.0)]);
    for half in halves {
        for a in anchors() {
            let start = a + Vec3::Y * half.y; // standing on the ground
            for &d in &ds {
                for len in [0.5f32, 2.0] {
                    s.push_str(&sweeps::row(w, half, start, d * len));
                    s.push_str(&sweeps::row(w, half, start - Vec3::Y * 0.01, d * len)); // sunk 1 cm, inside SKIN
                }
            }
        }
    }
    let mut r = Lcg(0x3E5A);
    for _ in 0..1500 {
        let half = Vec3::new(r.range(0.05, 0.4), r.range(0.05, 1.0), r.range(0.05, 0.4));
        let start = Vec3::new(r.range(-8.0, 8.0), half.y + r.range(0.0, 3.0), r.range(-8.0, 8.0));
        let delta = Vec3::new(r.axis(2.0), r.axis(2.0), r.axis(2.0));
        s.push_str(&sweeps::row(w, half, start, delta));
    }
    s
}

pub fn write_all(dir: &str) {
    let w = scene();
    let a = anchors();
    crate::common::write(dir, "mesh_scene.csv", scene_csv(&w));
    crate::common::write(dir, "mesh_sweeps.csv", sweeps_csv(&w));
    crate::common::write(dir, "mesh_slides.csv", slides::slides_csv(&w, &a, 0x3511));
    crate::common::write(dir, "mesh_trace_wall.csv", probes::trace_wall_csv(&w, &a, 0x3711));
    crate::common::write(dir, "mesh_find_ledge.csv", probes::find_ledge_csv(&w, &a, 0x3ED6));
}
```

In `main.rs`, add `mod mesh;`, and before `common::write_reference(&dir);` add `mesh::write_all(&dir);`.

Run: `cargo run --release` in `tools/golden`
Expected: it prints the five `mesh_*` files. All earlier files are unchanged: `git diff --exit-code -- RunlineCore/tests/golden/` shows only the new untracked files.

- [ ] **Step 7: Write the mesh golden replays, then run them**

In `Golden.h`, add `#include "runline/MeshWorld.h"` and, inside `namespace golden`:

```cpp
/// The triangle scene tools/golden used (mesh_scene.csv).
inline runline::MeshWorld MeshScene() {
    runline::MeshWorld w;
    for (const Row& r : Require("mesh_scene.csv", 1, 9)) w.tris.push_back(runline::Tri{V(r, 0), V(r, 3), V(r, 6)});
    return w;
}
```

At the end of `golden_tests.cpp`, add:

```cpp
TEST_CASE("golden: MeshWorld::Sweep matches faith_move") {
    const MeshWorld w = golden::MeshScene();
    const auto rows = golden::Require("mesh_sweeps.csv", 1000, 17);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        golden::CompareHit(rep, i, w.Sweep(golden::V(r, 0), golden::V(r, 3), golden::V(r, 6)), r, 9);
    }
    rep.Finish();
}
TEST_CASE("golden: SlideMove matches faith_move (mesh)") { CheckSlides(golden::MeshScene(), "mesh_slides.csv"); }
TEST_CASE("golden: TraceWall matches faith_move (mesh)") { CheckTraceWall(golden::MeshScene(), "mesh_trace_wall.csv"); }
TEST_CASE("golden: FindLedge matches faith_move (mesh)") { CheckFindLedge(golden::MeshScene(), "mesh_find_ledge.csv"); }
```

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: SUCCESS, with 47 test cases. The mesh tests are the slowest; under a minute in Debug is fine.

Mutation check (revert afterwards): in `MeshWorld.cpp`, change `if (depth < SKIN && ...` to `if (depth < 0.0f && ...`. Expected: `golden: MeshWorld::Sweep` FAILS (the sunk-1-cm rows), and so does the unit test for Review Focus 4. Revert, re-run: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add RunlineCore tools/golden/src
git commit -m "M1a: port MeshWorld with golden parity on a turned-off-grid scene

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Done when

- `powershell -ExecutionPolicy Bypass -File scripts/test.ps1` prints SUCCESS, with 47 test cases.
- Every golden file was generated from a clean faith-runner `9470d755`, and `REFERENCE` says so.
- Each task's mutation check made its golden test fail, and was reverted.
- `git grep -nE "std::(min|max)\(" -- RunlineCore/src RunlineCore/include` prints nothing.

**Next (M1b):** the controller. Before planning it, decide how its per-frame traces are produced: faith-runner's test helpers are `#[cfg(test)]`-only. The options are a local, never-committed patch to the clone that dumps traces from its own tests, or duplicating the scripts in the golden tool.
