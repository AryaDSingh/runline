# M3a: first-person arms (design)

> **Shelved 2026-10-11.** Built on branch m3a-arms (not merged); the user found the procedural arms
> much worse than the template's mocap arms. Superseded by M3b (2026-10-11-m3b-gasp-body-animation-design.md);
> the contact IK returns in M3c over animation.

Part of M3's feel pass (spec 2026-10-09, section 5). Research behind the choices:
docs/research/2026-10-10-free-assets.md.

## 1. Purpose

Make Runline feel like Mirror's Edge in first person: arms that react to every move, with the
signature moments (wallrun hand on the wall, hands on the ledge, the vault hand plant) landing on
the surfaces the simulation actually found.

Success: in a playtest every core state shows deliberate arm motion, nothing T-poses or snaps, the
hands sit on the real ledge, wall and vault contact, and the core still replays faith-runner bit-exact.

## 2. Decisions (from brainstorming, 2026-10-10)

| Topic | Decision |
|---|---|
| Order | Arms first (this spec, no downloads needed). Visible legs (GASP clips on the body) are the next milestone, M3b. The environment art pass comes after. |
| Approach | Procedural: per-state hand targets plus two-bone IK, either camera-relative or on the core's contact points. No keyed or mocap clips in M3a. |
| How faith-runner does it | Its default first-person arms are procedural sticks with world-anchored poses (src/viewmodel.rs); its optional mode plays Mirror's Edge's own animations from the player's install (faith_anim). We follow the procedural approach with real IK arms, and port no faith_anim code. |
| Implementation | A C++ anim instance whose proxy writes the pose (no Animation Blueprint graph). |
| Camera | The core's camera (CameraFx, SwanNeck) stays the authority; the arms follow the camera, never drive it. |
| Assets | Only the Manny arms already in the project. Nothing from Mirror's Edge. |

## 3. Architecture

All new code lives in Unreal/Runline/Source/Runline/Runner/Arms/. RunlineCore is unchanged: it stays a
faithful port, and this is new work.

- **`FRunlineArmsSolver`** (plain C++, no UObject). Each render frame it takes an
  `FRunlineArmsInput`:
  - the core state (with its timers and contacts), feet and view;
  - the rendered eye and rotation, and the interpolation alpha;
  - speed, look delta, render-frame dt (game-speed scaled);
  - the events from this frame's core steps.

  It returns an `FRunlineArmsPose`. For each hand: a target position and rotation (camera space or
  world space), an IK weight in [0, 1], and a finger shape (Relaxed, Fist, Open, Grip). Its own
  state is small: springs for sway and landing dip, the run-pump phase, the cross-fade between
  states. Given the same inputs it gives the same output.
- **`URunlineArmsAnimInstance`**: a C++ anim instance. It runs the solver on the game thread; its
  proxy builds the pose:
  - a camera-relative base pose (the "ready" stance);
  - the finger shapes, blended per hand;
  - two-bone IK per arm onto its target. Reach is clamped, so the arm never overextends; past reach,
    the weight fades to the base pose.

  It replaces ABP_FP_Copy on `FirstPersonMesh`.
- **`FRunlineArmsTuning`**: every spring constant, offset and timing in one struct, exposed as
  properties, so tuning is a data edit.
- **Attachment:** `FirstPersonMesh` attaches to the camera instead of the hidden body (which M3b will
  animate). The arms stay on the first-person render layer (their own FOV and scale; they don't clip
  into walls).
- **Events:** the movement component collects the events of every core step in a frame (through
  `OnStep`), so a hitch of several steps loses none.

### Contacts (all from the core; no new traces)

Converted to Unreal space only through RunlineUnits.

| Contact | Source |
|---|---|
| Ledge lip | `LedgeHang.ledgeY`, `normal` (the wall face is the feet moved `halfWidth` against the normal) |
| Shimmy | `Controller::shimmy`: `dir`, `t`, `from`, `to`, `corner` |
| Hand plant | `Vault.hand` is the feet at the plant; the ledge is that point less the VaultType's `handplant` offset (along the move, up) and the 90-unit half height |
| Wall plane | `WallRun` / `WallClimb` / `WallClimbTurned` `.normal`, plus the feet and the body radius |
| Ledge to ledge | `GrabTransfer.from`, `to`, `dur` |
| Top edge | `Traverse.to` (PullUp, Mantle) |

### Timing

The core steps at 60 Hz; the arms solve every render frame. Core timers are advanced by the
component's interpolation alpha, so timed reaches stay smooth at any frame rate. The arms are
cosmetic: nothing feeds back into the core.

## 4. Per-state behaviour

Camera-space targets are relative to the eye; world-space targets are on contacts, with IK.

| Core state | Hands | Notes |
|---|---|---|
| Ground (idle, run, sprint) | Camera-space ready pose; the run pump alternates hands | Pump phase from distance travelled (it can't drift from the steps); amplitude scales with speed; sway lags mouse look |
| Air | Hands rise and spread a little with fall speed | Jump event: a quick upward flick |
| SoftLand, Stunned (and the Land event) | A dip spring kicked by the landing impact; Stunned drops lower and recovers slowly | Uses the existing event payloads |
| Slide | Lead hand forward and low, bracing; the other back | Eases in over the slide's first 0.15 s |
| Roll | Both hands tuck to the chest (Fist) | The roll is a camera spin; the arms ride it |
| WallRun | The wall-side hand on the wall plane at shoulder height, sliding along it; the other hand pumps | Wall side from the normal against her facing; a short reach to plant at the start |
| WallClimb, WallClimbTurned | Hands alternate reaching up the wall plane (phase from the timer) | Turned: both pull off, then into Air |
| LedgeHang | Both hands on the lip, Grip | About shoulder width apart, either side of the eye's projection onto the lip |
| LedgeHang with `Controller::shimmy` set | Hands walk along the lip: the leading hand moves first, then the trailing one, timed by the shimmy's `t` | Round a corner (`corner` set): the leading hand goes to the new face |
| GrabTransfer | One hand reaches from the `from` lip to the `to` lip over `dur`; the other follows | Grip on arrival |
| Traverse (PullUp, Mantle) | Both hands planted on the top edge, pressing as she rises; released at the end | |
| Traverse (SpringBoard) | Ready pose, a forward swing at launch | A foot plant, no hand contact |
| Vault | Hand plant on `Vault.hand` through timeUp and timeOver, released on the way down; the high vaults use both hands | Phases from the VaultType's own times |
| LayOnGround | Hands out of view while lying; pushing on the ground plane while getting up | |

Shared rules:
- Every state change cross-fades over 0.1 to 0.2 s (in the tuning).
- An unreachable target fades its IK weight to 0; that hand returns to the camera-space pose.
- Any state without its own behaviour (and the NotPorted guard) holds the ready pose. Never a
  T-pose.

Shimmy is not a state of its own: the core keeps it in `Controller::shimmy` while in LedgeHang.

## 5. Edge cases

- Steep look pitch (up to ±90°): camera-space poses are clamped so the arms never fill the screen.
- Respawn, death and checkpoint snaps reset the solver's springs and cross-fades.
- Game speed (×1.25) and pause: the solver uses the component's scaled dt and freezes when paused.
- A hitch (up to MaxStepsPerFrame steps in one frame): all the events are seen; springs are
  sub-stepped so a large dt stays stable.

## 6. Testing

1. **Solver tests** (Runline.Arms.*), on hand-made core states:
   - LedgeHang: both palms within 2 cm of the lip, either side of the eye.
   - WallRun: the wall-side hand is the one on the wall, and it is on the plane.
   - Vault: the plant hand at `Vault.hand` through timeUp and timeOver, released after.
   - Ground: the pump phase follows distance; still arms when standing still.
   - An unreachable target fades its weight to 0.
   - Every state change blends with no frame-to-frame jump above a set limit.
   - Snaps reset the springs; a hitch's events all register.
2. **Pose tests on the real mesh:** evaluate the anim instance in a test world and read back the hand
   bones: on their targets within tolerance, arm length never exceeded.
3. **Replay:** faith-runner's traces (wallrun, hang, vault, traverse) through the component: no NaNs,
   no IK snaps at state changes, and the core still bit-exact (the arms don't feed back).
4. **Playtest:** a screenshot per state (`?shot=`), then your feel pass.

## 7. Out of scope

- Legs and the visible body (M3b), and any Mixamo or keyed clips (the per-state slot leaves room).
- Melee and takedowns, ziplines, poles (the core doesn't run them yet; M4).
- Sound.
- The environment art pass.
