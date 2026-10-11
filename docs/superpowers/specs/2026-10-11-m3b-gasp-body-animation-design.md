# M3b: GASP body animation (design)

Part of M3's feel pass (spec 2026-10-09, section 5). It replaces M3a (2026-10-10-m3a-first-person-arms-design.md),
which was shelved on 2026-10-11: the user found the procedural arms much worse than the template's
mocap-driven arms. Branch m3a-arms is kept unmerged; its contact IK returns in M3c, layered over
animation.

## 1. Purpose

Give the runner real motion-capture animation: Epic's Game Animation Sample (GASP) clips on the body,
played by the core's state. The first-person arms keep copying the body's pose (what the user liked),
so they get the better motion too; looking down shows her hips and legs.

Success: every core state plays a fitting clip (or a marked stand-in), nothing T-poses, timed moves
match the core exactly, and the user, judging the pose-capture screenshots and a playtest, finds it
better than today.

## 2. Decisions (from brainstorming, 2026-10-11)

| Topic | Decision |
|---|---|
| Source | GASP 5.7 sample, created by the user as a UE 5.8 project at `D:\GameAnimationSample` (free, Epic). Its clips are authored on the UEFN mannequin and retargeted to our UE5 Manny with GASP's own `RTG_UEFN_to_UE5_Mannequin`. The Tibeuru FBX re-export pack is not used. |
| Approach | Clips picked per core state, timed by the core. No motion matching (it fits Epic's movement, not ours, and has no parkour moves). |
| Logic | A native C++ anim instance plus a `URunlineAnimSet` data asset (the state-to-clip table, editable in the editor). No Animation Blueprint. |
| Repository | The repo will be public: GASP content stays out of git. Clips live in the git-ignored `Content/ThirdParty/GASP`, made by a committed import script; without them the game falls back to today's animation. |
| Missing states | Wallrun, wallclimb, ledge hang, shimmy, grab transfer and getting up have no GASP clip: marked stand-ins now; Mixamo clips plus the hands-on-ledges IK come in M3c. |
| Checking looks | A pose-capture mode screenshots every core state. Claude reviews every image before calling the work done, and the user gets the set before the playtest. |

## 3. Architecture

### Import (scripted, run once per machine)

- `Data/Animation/gasp-clips.txt` (committed): the GASP clip names used (about 40 to 60).
- `scripts/import-gasp.ps1 [-Gasp D:\GameAnimationSample]` runs two headless editor passes:
  1. In the GASP project: a Python script (`tools/gasp/export_clips.py`) batch-retargets each listed
     clip from the UEFN mannequin to the UE5 Manny with `RTG_UEFN_to_UE5_Mannequin` and exports the
     results as FBX to the git-ignored `build/gasp-fbx/`.
  2. In Runline: the `RunlineImportGasp` commandlet (RunlineEditor) imports those FBX onto
     `/Game/Characters/Mannequins/Meshes/SK_Mannequin` into `/Game/ThirdParty/GASP`, root motion
     off (the core moves her), and records each clip's authored root speed (metres per second, from
     the retargeted root track) for the play-rate rule.
- Pass 1 needs Unreal's Python Editor Script Plugin in the GASP project. It isn't in that project's
  .uproject, so enabling it is a one-line change to the user's GASP copy (asked before doing it);
  the plan's first task checks the whole pass on one clip before anything else is built.
- FBX in the middle because asset files can't be copied between the projects (different skeleton
  paths). It drops GASP's notifies and sync markers, which nothing here uses.
- `.gitignore`: `Unreal/Runline/Content/ThirdParty/` and `build/gasp-fbx/`.

### `URunlineAnimSet` (data asset, committed; references the git-ignored clips softly)

One entry per slot (Idle, Walk, Run, Sprint, CrouchIdle, CrouchWalk, JumpStand, JumpRun, JumpSprint,
Fall, LandStandLight/Heavy, LandRunLight/Heavy, LandSprintLight/Heavy, Stumble, Roll, SlideInto,
SlideLoop, SlideOut, Vault, Mantle, StepUp, PullUp, Springboard, and the stand-ins), each with the
clip (soft reference), its authored speed where it loops, and a `bStandIn` flag. Soft references
let the asset load without the clips.

### `URunlineBodyAnimInstance` (C++, native proxy, no anim graph)

Each frame it reads the core's state, timers, speed and the frame's events (the movement component
already exposes `GetFrameEvents`, `GetStepAlpha` and `GetSnapCount` on the shelved branch; M3b
brings those three accessors over), picks the slots and their play positions, and evaluates and
cross-fades the sequences in its proxy. With no anim set or no clips loaded it is not used: the body
keeps `ABP_Unarmed`.

### Meshes

- Body: hidden from her, casts her shadow (as today), runs `URunlineBodyAnimInstance`.
- Arms: unchanged; `ABP_FP_Copy` copies the body's pose.
- Legs (new): a third Manny attached to the body, copying its pose with everything from `spine_03`
  up hidden, seen only by her, casting no shadow. Looking down shows hips and legs.

## 4. Per-state clips and timing

| Core state | Slot (GASP clip) | Timing |
|---|---|---|
| Ground: idle, walk, run, sprint | Idle, Walk, Run, Sprint forward loops, blended by speed | Cycle rate = her horizontal speed / the clip's authored speed, held to 0.7 to 1.4 |
| Ground, crouched | CrouchIdle, CrouchWalk | Same rule |
| Jump event | JumpStand / JumpRun / JumpSprint by speed, one-shot | Then Fall |
| Air | Fall (Jump Loop Fall) | Loops |
| Land event, SoftLand | Land{Stand,Run,Sprint}{Light,Heavy} by speed and impact, one-shot over the locomotion | Fades out at its end |
| Stunned | Stumble | Stretched over `hardLandStun` (1.8 s) |
| Roll | Roll (Jump F Land Roll) | Stretched over `rollTime` (0.5 s) |
| Slide | SlideInto then SlideLoop; SlideOut on leaving it | Into over 0.25 s, Loop while sliding |
| Vault (kinds 2 to 5) | Vault (over), Mantle (onto) | Whole clip over `Vault::Duration()` |
| Vault (kinds 0, 1: step-ups) | StepUp (the low mantle) | Over `Duration()` |
| Traverse: PullUp | PullUp (Climb Start 2.5, stand) | Over `dur` (`pullupTime` 0.55 s) |
| Traverse: Mantle | Mantle | Over `dur` |
| Traverse: SpringBoard | Springboard (Jump F Start Run) | Over `dur` (`springboardStepTime` 0.4 s) |
| Stand-in: WallRun | Sprint loop, the body leaned 20 degrees from the wall | Locomotion rule |
| Stand-in: WallClimb, WallClimbTurned | PullUp, then JumpRun when turned | Over the climb timer |
| Stand-in: LedgeHang (and shimmy), GrabTransfer | Catch Cliff, held at its hanging frame | Held |
| Stand-in: LayOnGround | Stumble's last frame; played back to standing while getting up | Over `layOnGroundGetUpTime` (1.33 s) |

Shared rules:
- State changes cross-fade over 0.15 s (0.08 s into a slide or a roll).
- One-shots are positioned by the core's timers (plus the step alpha), never by summed frame times;
  when the core's move ends, the clip ends with a cross-fade.
- A slot without a loaded clip falls back to the locomotion loop and logs one warning per slot.

## 5. Edge cases

- Respawn, death: a snap (the snap counter) cuts to the new state with no blend.
- Game speed (x1.25) and pause: clip time uses the core's scaled time; paused means frozen.
- Hitch frames: one-shots follow the core's timers, so they can't drift.
- Looking down: the legs mesh follows the body, which already follows the interpolated eye.

## 6. Testing

1. **Clip choice** (Runline.Body.*, no clips needed: a test anim set with dummy sequences): the
   right slot and play position for hand-made core states. A vault half way through `Duration()` is at
   50% of its clip; 5 m/s on a 4 m/s run clip plays at 1.25; a heavy landing picks Heavy; a roll
   spans `rollTime`.
2. **Fallbacks:** no anim set keeps `ABP_Unarmed`; a missing slot uses the locomotion loop and warns
   once; a state change cross-fades.
3. **Import:** the clip list and the anim set agree; after import every listed clip exists (the
   test skips, with a message, when the GASP clips aren't on the machine).
4. **Pose capture:** `?poses=1` puts her in every core state in turn and screenshots each. Claude
   reads every image and fixes what looks wrong; the user gets the set before the playtest.
5. **Parity:** the trace replays stay bit-exact (animation only reads the core).

## 7. Out of scope

- Mixamo clips and the hands-on-ledges IK (M3c).
- Motion matching, foot IK on slopes, sound.
- The environment art pass.
