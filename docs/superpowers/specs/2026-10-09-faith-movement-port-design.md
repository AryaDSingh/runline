# Faith (working title): design

Date: 2026-10-09 · Status: draft for review

## 1. Purpose

A portfolio piece for **gameplay programmer / technical designer** roles. It shows two things:

1. **Engineering:** Mirror's Edge's first-person movement, ported faithfully to Unreal Engine 5 C++ and proven by tests.
2. **Design judgement:** one or two *new* moves that fit Mirror's Edge's momentum rules, designed, tuned and tested the same way.

Pitch: *"I ported Mirror's Edge's movement to UE5, then designed new moves that fit its rules."*

**Done line** (anything past it is optional):
- the movement slice, solid and feeling right
- one or two new momentum moves
- one polished level with a time trial
- a 60-second video and a case-study write-up

## 2. Decisions

| Topic | Decision |
|---|---|
| Engine | Unreal Engine 5.7, C++ (Blueprints only for level scripting). Verse/UE6 and Bevy were considered and rejected for career relevance. |
| Source | `tnrjns/faith-runner` (`crates/faith_move`, Rust). The author granted permission on 2026-10-08 to use, modify and port the code and tuning, with credit. Keep a link to the GitHub issue thread as the record. |
| Excluded | Combat (melee, takedowns), Mirror's Edge assets, decompiled game scripts, gliding, blinks and timestop. |
| Timeline | About 6 months at 10–15 h/week, starting the week of 2026-10-12, with a go/no-go checkpoint at about 2027-01-17 (moved from 2026-12-06 to keep M1's full scope; decided 2026-10-09). |
| Art | Simple, cheap and readable: greybox-plus with a strong palette and marketplace assets. The richer ideas from brainstorming (decay-to-dream world, dissolving pixel rendering) are parked until after M3. |

## 3. Architecture

```
FaithCore/          plain C++20 static library: no Unreal, builds with CMake
  World.h           interface: Sweep(half, start, delta) -> optional<SweepHit>, Overlaps(Aabb), Fixtures()
  BoxWorld, MeshWorld   test worlds (port of world.rs)
  Controller        state machine (port of controller.rs) + locomotion, vault, climb, look, ...
  Tuning.h          every number, keeping the `ME:` / `guess` provenance comments
  CameraFx          bob, shake, tilt, FOV (port of camera.rs)
FaithCore/tests/    ported scripted-input tests + golden-trace comparisons (doctest)
tools/golden/       small Rust program, run against faith-runner, that writes per-frame traces
Unreal/Faith/       UE5 project
  UFaithMovementComponent   UCharacterMovementComponent custom mode; ticks FaithCore at a fixed 60 Hz
  FUnrealWorld      World implemented with UWorld sweep/overlap queries (unit conversion: m <-> cm, Y-up <-> Z-up)
  Input, camera, arms, greybox importer (Editor Python: builds levels from box lists)
```

**Unit boundary.** FaithCore works in metres with Y up, as `faith_move` does. All conversion happens in one place, `FUnrealWorld` and the component. Nothing else in the Unreal layer touches raw core coordinates. The M2 plan must write down the exact axis mapping (core x/y/z to Unreal X/Y/Z, accounting for Unreal's left-handed frame) and the yaw/pitch convention, and test both.

**Fixed step.** The core always steps at 1/60 s, as faith-runner's tests do. The component accumulates frame time and runs whole steps. **Mouse look is not stepped:** raw yaw and pitch are applied every render frame and the accumulated look is fed to the core each step, so look never lags on high-refresh displays. Only the camera and arms are interpolated between steps; the capsule stays at the real stepped position, so Unreal-side triggers (checkpoints, time-trial gates) don't lag.

**Port fidelity.** Port the Rust function by function, keeping its names and comments so the two can be compared side by side. Keep glam's order of operations (for example in `normalize` and `lerp`), because a different order changes float results. Only make it more idiomatic where C++ forces a change: enums for move states, and `std::optional` in place of `Option`.

**Combat stays in the code paths.** Melee is woven through `controller.rs` (around 74 references). Port those paths but always feed the melee and takedown inputs as false, so the controller's structure, and its golden traces, stay comparable with the Rust original.

## 4. Testing

1. **Ported unit and route tests:** `tests.rs`, `course_tests.rs`, `rooftops_tests.rs`, `moves_tests.rs`, `springboard_tests.rs`, `mesh_tests.rs` and `rough_shapes_tests.rs`, rewritten as doctest cases. Each milestone's plan lists which of these tests it owns, since many in `tests.rs` cover M4 moves.
2. **Golden traces:** for each scripted test, record the Rust controller's state every frame (feet, velocity, state, events). Collision queries (M0) match within 1e-5. For controller traces, **state and events must match exactly** and positions within 1e-4 m, and the report names the first frame and field that diverge. (A tiny float difference that flips a state threshold makes traces diverge completely, so the first divergence is what matters, not the tolerance.) The Rust test helpers are `#[cfg(test)]`-only, so the M1 plan must decide how traces are produced: either duplicate the scripts in the golden tool, or apply a local, never-committed patch to the reference clone that dumps traces from its own tests.
3. **One command** (`scripts/test.ps1`) builds FaithCore and runs everything, with no Unreal needed.
4. **In Unreal:** a few automation tests that load the greybox map and run scripted inputs through the real component. These check the adapter, not the logic.
5. **Feel:** checked only by playtesting (you, plus 3–5 classmates at M3).

## 5. Milestones

| # | Weeks (dates) | Deliverable |
|---|---|---|
| M0 | 1 (Oct 12–18) | Git and LFS repo, UE 5.7 C++ project, CMake FaithCore skeleton, doctest, `CLAUDE.md`, `CREDITS.md`. Rust installed, faith_move's tests pass, the golden tool writes collision golden data. |
| M1 | 2–7 (Oct 19–Nov 29) | FaithCore: World, BoxWorld, MeshWorld (triangle scenes, needed for `mesh_tests`) and collide-and-slide, locomotion, jump and landing, vault and step-up, wallrun, wallclimb, ledge, slide, roll, 180 turn and kick. All their tests and golden traces pass. Split into several plans (collision, then locomotion and jump, then the wall moves, then the rest). |
| M2 | 8–10 (Nov 30–Dec 20) | In Unreal: the component, FUnrealWorld, Enhanced Input (keyboard and controller), CameraFx, template first-person arms, and faith-runner's Rooftops map imported by script. Light weeks expected (exams). |
| — | 11–12 (Dec 21–Jan 3) | Holiday buffer: no planned work. |
| M3 | 13–14 (Jan 4–17) | **Checkpoint (about 2027-01-17):** feel pass, playtests, clip, short write-up. Go/no-go. |
| M4 | 15–18 (Jan 18–Feb 14) | The remaining ported moves (springboard, swing, zipline, balance, coil, dodge, barge, wall-to-wall, corner shimmy), plus Reaction Time (`reaction.rs`). |
| M5 | 17–20 (Feb 1–28) | **New momentum moves:** design 1–2 (candidates: a grapple line that preserves momentum, a dash that spends sprint energy, a wall-bounce chain), each with written rules, tuning and tests. |
| M6 | 19–23 (Feb 15–Mar 21) | One level built to a movement metrics sheet, with a time trial (ghost run, instant restart, generous checkpoints). |
| M7 | 24–26 (Mar 22–Apr 11) | Video, case study, README, release build, credits. **Done.** |

The schedule now has no slack beyond the holiday buffer. If time runs short, trim M4 first (balance, barge, shimmy), then cut M5 to one move. M7 is never cut.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Unreal's collision answers differently from BoxWorld (sweep edge cases, starting inside geometry) | Write adapter tests at M2 that compare FUnrealWorld with BoxWorld on the same boxes. |
| Coordinate or unit bugs | Keep all conversion in one place, with tests on it. |
| Feel differs at variable frame rate | Fixed 60 Hz stepping with interpolation. |
| Scope creep from new ideas | Ideas go in a backlog. Nothing beyond M3 is revisited until the checkpoint. |
| Not knowing Unreal C++ well | Template project first, and lean on the clean adapter seam. |

## 7. Credits

`CREDITS.md` from day one: faith-runner (tnrjns, with a link to the permission thread), Mirror's Edge (DICE/EA) as the design reference, and every asset with its license. The README and video description carry the same faith-runner credit.

## 8. Parked ideas (after M3)

- A decay-to-dream world, with rendering that dissolves into painted pixels (references: Project Shadowglass, Sable, Eclipsium)
- A feel that shifts from desperate to free
- Story
- Levels 2–3
- An animated full body
