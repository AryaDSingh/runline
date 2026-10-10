#include <doctest/doctest.h>
#include "runline/BoxWorld.h"
#include "runline/Camera.h"
#include "runline/Controller.h"
using namespace runline;

// Every committed trace also checks CameraFx against faith_move frame by frame (trace v5 `fx`,
// replay_tests.cpp); these cover what traces may not reach.

namespace {
constexpr float DT = 1.0f / 60.0f;
BoxWorld Floor() {
    BoxWorld w;
    w.Add(Aabb::New({-50, -1, -50}, {50, 0, 50}));
    return w;
}
} // namespace

TEST_CASE("trauma stacks up to 1, and the shake is its square") {
    CameraFx fx;
    fx.AddTrauma(0.7f);
    fx.AddTrauma(0.7f);
    const Controller c(Tuning{}, {0, 5, 0}, 0.0f);
    const Shot s = fx.Update(0.0f, c, Input{});
    CHECK(s.shake == 1.0f);
}

TEST_CASE("dying clears the shake and the roll somersault") {
    const BoxWorld w = Floor();
    Controller c(Tuning{}, {0, 0, 0}, 0.0f);
    c.Step(DT, Input{}, w);
    CameraFx fx;
    // A roll starts the somersault and some trauma.
    c.events = {Event{EventKind::Roll}};
    Shot s = fx.Update(DT, c, Input{});
    CHECK(s.shake > 0.0f);
    c.events = {};
    s = fx.Update(DT, c, Input{});
    CHECK(s.view.pitch < -0.01f);   // mid-somersault
    c.events = {Event{EventKind::Death}};
    s = fx.Update(DT, c, Input{});
    CHECK(s.shake == 0.0f);
    c.events = {};
    s = fx.Update(DT, c, Input{});
    CHECK(s.view.pitch == doctest::Approx(c.GetView().pitch).epsilon(0.01));   // no somersault left
}

TEST_CASE("the animation-driven settings leave only the shake and a light landing kick") {
    const CameraFxSettings a = CameraFxSettings::AnimationDriven();
    CHECK(a.bobVertical == 0.0f);
    CHECK(a.wallrunRollDeg == 0.0f);
    CHECK_FALSE(a.rollFlip);
    CHECK(a.landingKick == 0.5f);
    CHECK(a.sprintShake == 0.08f);
    CHECK(a.fovBase == CameraFxSettings{}.fovBase);
}
