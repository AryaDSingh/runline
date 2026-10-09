#include "runline/Controller.h"

// Moves and checks not ported yet. Each stands in for one faith_move function and gives the
// answer that function gives when there's nothing there:
//   - checks answer "no" (false / nullopt), which is right wherever the real check would find
//     nothing (a flat floor, open air, a world with no fixtures);
//   - states throw NotPorted, so a replay that reaches one fails loudly instead of drifting.
// The replay tests are what make this safe: if the real check would have fired, the replay
// diverges from faith-runner at that frame.

namespace runline {

// NOT PORTED: vertigo.rs vertigo_input (M4, with TdMove_Vertigo). Only blanks input while in
// the Vertigo state, which nothing ported can enter.
void Controller::VertigoInput(Input&) const {}

// NOT PORTED: melee.rs melee_pressed_again / controller.rs start_melee (M4, attacks). With no
// attack in progress and none started, both leave the controller as it is.
void Controller::MeleePressedAgain() { throw NotPorted("melee_pressed_again"); }
void Controller::StartMelee() { throw NotPorted("start_melee"); }

// NOT PORTED: takedown.rs try_takedown (M4).
bool Controller::TryTakedown(const World&) { throw NotPorted("try_takedown"); }

// NOT PORTED: climb.rs try_into_climb (M4, ladders): needs a Ladder fixture; there are none.
bool Controller::TryIntoClimb(const Input&, const World&) { return false; }

// NOT PORTED: rumpslide.rs try_rump_slide (M4): needs ground between 0.6 and WALKABLE steep.
bool Controller::TryRumpSlide(const World&) { return false; }

// NOT PORTED: controller.rs try_balance (M4): needs a Beam fixture; there are none.
bool Controller::TryBalance(const World&) { return false; }

// NOT PORTED: stepup.rs try_auto_step_up (M4). Only called when Tuning::autoStepUp is on, which is
// exactly when the real check matters, so it fails loudly rather than answer "no".
bool Controller::TryAutoStepUp(const World&) { throw NotPorted("try_auto_step_up (Tuning::autoStepUp is on)"); }

// NOT PORTED: controller.rs try_springboard / springboard_run_in (M4, springboard).
bool Controller::TrySpringboard(const World&) { return false; }
void Controller::SpringboardRunIn(float, const World&) { throw NotPorted("springboard_run_in"); }

// NOT PORTED: vertigo.rs try_vertigo (M4): needs a long drop just ahead while walking.
bool Controller::TryVertigo(const World&) { return false; }

// NOT PORTED: controller.rs try_grab_fixture (M4): ziplines and swing poles; there are none.
bool Controller::TryGrabFixture(const World&) { return false; }

// NOT PORTED: grabtransfer.rs try_grab_transfer (M1d if a selected trace needs it, else M4): a
// ledge-to-ledge transfer when pushing up on a hang.
bool Controller::TryGrabTransfer(Vec3, float, const World&) { return false; }

// NOT PORTED: vault.rs plan (M1d, vaults): the jump's vault check.
bool Controller::TryVault(const Input&, const World&) { return false; }

// NOT PORTED: controller.rs soft_below / check_barbed_wire (M4): both look for fixtures (SoftPad,
// BarbedWire), and the C++ World has none until fixtures are ported.
bool Controller::SoftBelow(const World&, float) const { return false; }
void Controller::CheckBarbedWire(const World&) {}

// NOT PORTED: the states the core can enter but doesn't run yet (M1d: Slide, Roll; M4: SoftLand, LayOnGround).
void Controller::UnportedState(const State& s, float, const Input&, const World&) {
    throw NotPorted(std::string("state ") + StateName(s));
}

} // namespace runline
