// The body chooser (Unreal/Runline/Source/Runline/Runner/Body/RunlineBodyChooser.h, M3b Task 4):
// the plan's Runline.Body.* chooser tests, as doctest cases, so they run with the core's on every
// platform. The chooser is plain C++ and needs no clips: every clip here is a 1 s dummy.
#include <doctest/doctest.h>

#include "RunlineBodyChooser.h"

#include <cmath>
#include <set>

namespace {

constexpr float Frame = 1.0f / 60.0f;

/// Every clip loaded, 1 s long; loops authored at walk 1.5, run 4, sprint 6, crouch 1 m/s.
FRunlineBodyChooser Chooser(std::set<ERunlineAnimSlot> Missing = {}) {
    FRunlineBodyChooser C;
    C.Clips = [Missing](ERunlineAnimSlot S) {
        FRunlineClipInfo I;
        I.Length = 1.0f;
        I.bLoaded = !Missing.count(S);
        I.AuthoredSpeed = S == ERunlineAnimSlot::Walk ? 1.5f : S == ERunlineAnimSlot::Run ? 4.0f
            : S == ERunlineAnimSlot::Sprint ? 6.0f : S == ERunlineAnimSlot::CrouchWalk ? 1.0f : 0.0f;
        I.HoldAt = S == ERunlineAnimSlot::Hang ? 0.35f : 0.0f;
        return I;
    };
    return C;
}

FRunlineBodyInput In(const runline::State& S, float Speed = 0.0f) {
    FRunlineBodyInput I;
    I.State = S;
    I.Vel = {0.0f, 0.0f, -Speed};
    I.Dt = Frame;
    return I;
}

FRunlineBodyPose Settle(FRunlineBodyChooser& C, const FRunlineBodyInput& I, int N = 60) {
    FRunlineBodyPose P;
    for (int K = 0; K < N; ++K) P = C.Choose(I);
    return P;
}

/// The layer of slot S (weight 0 if absent).
FRunlineBodyLayer Layer(const FRunlineBodyPose& P, ERunlineAnimSlot S) {
    for (const FRunlineBodyLayer& L : P.Layers)
        if (L.Slot == S) return L;
    return {S, 0.0f, 0.0f};
}

float Total(const FRunlineBodyPose& P) {
    float W = 0;
    for (const FRunlineBodyLayer& L : P.Layers) W += L.Weight;
    return W;
}

runline::Event Land(float Impact) {
    runline::Event E{runline::EventKind::Land};
    E.impact = Impact;
    return E;
}

} // namespace

// Running at 4.5 m/s: between the run (4) and sprint (6) loops, at one shared phase; the cycle plays
// at 4.5 / 4 the run clip's rate (it dominates).
TEST_CASE("body: locomotion by speed (Runline.Body.LocomotionBySpeed)") {
    FRunlineBodyChooser C = Chooser();
    const FRunlineBodyPose P = Settle(C, In(runline::Ground{}, 4.5f));
    const FRunlineBodyLayer Run = Layer(P, ERunlineAnimSlot::Run), Sprint = Layer(P, ERunlineAnimSlot::Sprint);
    CHECK(Total(P) == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(Run.Weight == doctest::Approx(0.75f).epsilon(1e-3));
    CHECK(Sprint.Weight == doctest::Approx(0.25f).epsilon(1e-3));
    CHECK(Run.Time == doctest::Approx(Sprint.Time).epsilon(1e-4));
    const float Before = Run.Time;
    const FRunlineBodyPose Q = C.Choose(In(runline::Ground{}, 4.5f));
    CHECK(std::fmod(Layer(Q, ERunlineAnimSlot::Run).Time - Before + 1.0f, 1.0f) == doctest::Approx(Frame * 4.5f / 4.0f).epsilon(1e-4));
    CHECK(Layer(Settle(C, In(runline::Ground{}, 0.0f)), ERunlineAnimSlot::Idle).Weight == doctest::Approx(1.0f).epsilon(1e-4));
}

// Review Focus 4: a vault half way through its Duration() is half way through its clip, whatever
// the frame times; the step alpha counts.
TEST_CASE("body: one-shots follow the core timer (Runline.Body.OneShotsFollowTheCoreTimer)") {
    FRunlineBodyChooser C = Chooser();
    runline::Vault V;
    V.kind = 3;   // VaultOver
    V.t = V.Duration() * 0.5f;
    FRunlineBodyInput I = In(V);
    I.Dt = 0.25f;   // a hitch
    CHECK(Layer(Settle(C, I, 3), ERunlineAnimSlot::Vault).Time == doctest::Approx(0.5f).epsilon(1e-4));
    I.StepAlpha = 0.5f;
    CHECK(Layer(C.Choose(I), ERunlineAnimSlot::Vault).Time == doctest::Approx((V.t + 0.5f * I.StepSeconds) / V.Duration()).epsilon(1e-4));
    runline::Roll R;
    R.t = I.CoreTuning.rollTime * 0.25f;
    CHECK(Layer(Settle(C, In(R)), ERunlineAnimSlot::Roll).Time == doctest::Approx(0.25f).epsilon(1e-4));
    V.kind = 1;   // a step-up
    V.t = 0.0f;
    CHECK(Layer(Settle(C, In(V)), ERunlineAnimSlot::StepUp).Weight > 0.99f);
    V.kind = 2;   // VaultOnto: a mantle
    CHECK(Layer(Settle(C, In(V)), ERunlineAnimSlot::Mantle).Weight > 0.99f);
}

// Landings pick by speed and impact, play over the locomotion, and fade out.
TEST_CASE("body: landings by speed and impact (Runline.Body.LandingsBySpeedAndImpact)") {
    FRunlineBodyChooser C = Chooser();
    Settle(C, In(runline::Ground{}, 6.5f));
    FRunlineBodyInput I = In(runline::Ground{}, 6.5f);
    I.Events = {Land(9.0f)};
    const FRunlineBodyPose P = C.Choose(I);
    CHECK(Layer(P, ERunlineAnimSlot::LandSprintHeavy).Weight > 0.99f);
    I.Events.clear();
    const FRunlineBodyPose After = Settle(C, I, 70);
    CHECK(Layer(After, ERunlineAnimSlot::LandSprintHeavy).Weight == doctest::Approx(0.0f));
    FRunlineBodyChooser D = Chooser();
    Settle(D, In(runline::Ground{}, 0.5f));
    FRunlineBodyInput J = In(runline::Ground{}, 0.5f);
    J.Events = {Land(3.0f)};
    CHECK(Layer(D.Choose(J), ERunlineAnimSlot::LandStandLight).Weight > 0.99f);
}

// Review Focus 2: a move the core ends early doesn't run on: its clip cross-fades out.
TEST_CASE("body: the clip ends with the move (Runline.Body.ClipEndsWithTheMove)") {
    FRunlineBodyChooser C = Chooser();
    runline::Vault V;
    V.kind = 3;
    V.t = 0.1f;
    Settle(C, In(V), 10);
    const FRunlineBodyPose First = C.Choose(In(runline::Ground{}, 4.0f));
    CHECK(Layer(First, ERunlineAnimSlot::Vault).Weight > 0.8f);   // still mostly the vault: a cross-fade
    const FRunlineBodyPose Later = Settle(C, In(runline::Ground{}, 4.0f), 12);
    CHECK(Layer(Later, ERunlineAnimSlot::Vault).Weight == doctest::Approx(0.0f));   // gone after 0.15 s
    CHECK(Total(First) == doctest::Approx(1.0f).epsilon(1e-4));
}

// Review Focus 3: a respawn cuts: no fade from the old pose.
TEST_CASE("body: a snap cuts (Runline.Body.SnapCuts)") {
    FRunlineBodyChooser C = Chooser();
    Settle(C, In(runline::Roll{0.2f}));
    FRunlineBodyInput I = In(runline::Ground{});
    I.SnapCount = 1;
    const FRunlineBodyPose P = C.Choose(I);
    CHECK(Layer(P, ERunlineAnimSlot::Roll).Weight == 0.0f);
    CHECK(Layer(P, ERunlineAnimSlot::Idle).Weight == doctest::Approx(1.0f).epsilon(1e-6));
}

// Review Focus 5: a missing clip falls back to the locomotion loop, with one warning per slot.
TEST_CASE("body: a missing slot uses locomotion (Runline.Body.MissingSlotUsesLocomotion)") {
    FRunlineBodyChooser C = Chooser({ERunlineAnimSlot::Roll});
    std::set<ERunlineAnimSlot> Logged;
    C.OnMissingClip = [&](ERunlineAnimSlot S) { CHECK(Logged.insert(S).second); };
    const FRunlineBodyPose P = Settle(C, In(runline::Roll{0.1f}, 4.0f));
    CHECK(Layer(P, ERunlineAnimSlot::Roll).Weight == 0.0f);
    CHECK(Layer(P, ERunlineAnimSlot::Run).Weight == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(C.Warnings() == 1);
    CHECK(Logged.size() == 1);
}

// Stand-ins and the slide: hang held at HoldAt; wallrun leans away from the wall; the slide's
// into-then-loop.
TEST_CASE("body: stand-ins and the slide (Runline.Body.StandInsAndSlide)") {
    FRunlineBodyChooser C = Chooser();
    const FRunlineBodyPose Hang = Settle(C, In(runline::LedgeHang{{0.0f, 0.0f, 1.0f}, 2.0f, false}));
    CHECK(Layer(Hang, ERunlineAnimSlot::Hang).Time == doctest::Approx(0.35f).epsilon(1e-4));
    // The wall's normal points -X: the wall is on her right as she faces -Z (core yaw 0).
    const FRunlineBodyPose Run = Settle(C, In(runline::WallRun{{-1.0f, 0.0f, 0.0f}, 0.3f}, 6.0f));
    CHECK(Layer(Run, ERunlineAnimSlot::Sprint).Weight > 0.99f);
    CHECK(Run.LeanDegrees == doctest::Approx(-20.0f).epsilon(1e-3));   // away from the wall: left
    CHECK(Settle(C, In(runline::WallRun{{1.0f, 0.0f, 0.0f}, 0.3f}, 6.0f)).LeanDegrees == doctest::Approx(20.0f).epsilon(1e-3));
    CHECK(Settle(C, In(runline::Ground{}, 4.0f)).LeanDegrees == doctest::Approx(0.0f));
    CHECK(Layer(Settle(C, In(runline::Slide{0.1f})), ERunlineAnimSlot::SlideInto).Weight > 0.99f);
    CHECK(Layer(Settle(C, In(runline::Slide{0.6f})), ERunlineAnimSlot::SlideLoop).Weight > 0.99f);
}

// Beyond the plan's list: a jump plays its start by speed, then the fall loop.
TEST_CASE("body: a jump starts by speed, then falls") {
    FRunlineBodyChooser C = Chooser();
    Settle(C, In(runline::Ground{}, 4.0f));
    FRunlineBodyInput I = In(runline::Air{}, 4.0f);
    I.Events = {runline::Event{runline::EventKind::Jump}};
    C.Choose(I);
    I.Events.clear();
    const FRunlineBodyPose Up = Settle(C, I, 20);
    CHECK(Layer(Up, ERunlineAnimSlot::JumpRun).Weight > 0.99f);
    CHECK(Layer(Up, ERunlineAnimSlot::JumpRun).Time == doctest::Approx(20 * Frame).epsilon(1e-3));
    CHECK(Layer(Settle(C, I, 60), ERunlineAnimSlot::Fall).Weight > 0.99f);   // past the 1 s start
}

// Beyond the plan's list: leaving a slide on her feet plays SlideOut over the locomotion.
TEST_CASE("body: a slide ends with SlideOut") {
    FRunlineBodyChooser C = Chooser();
    Settle(C, In(runline::Slide{0.6f}, 5.0f));
    const FRunlineBodyPose Out = C.Choose(In(runline::Ground{}, 4.0f));
    CHECK(Layer(Out, ERunlineAnimSlot::SlideOut).Weight > 0.99f);
    CHECK(Layer(Settle(C, In(runline::Ground{}, 4.0f), 70), ERunlineAnimSlot::SlideOut).Weight == 0.0f);
}

// Beyond the plan's list: lying is the stumble's last frame; getting up plays it back to standing.
TEST_CASE("body: lying down, and getting up") {
    FRunlineBodyChooser C = Chooser();
    CHECK(Layer(Settle(C, In(runline::LayOnGround{0.5f, std::nullopt, false})), ERunlineAnimSlot::Stumble).Time == doctest::Approx(1.0f));
    runline::LayOnGround Up{1.0f, 0.0f, false};
    Up.gettingUp = runline::Tuning{}.layOnGroundGetUpTime * 0.25f;
    CHECK(Layer(Settle(C, In(Up)), ERunlineAnimSlot::Stumble).Time == doctest::Approx(0.75f).epsilon(1e-4));
}
