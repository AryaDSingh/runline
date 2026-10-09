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
    // respawn_on_death is on by default. The respawn happens mid-step and the frame's remaining
    // substep runs on (as in faith_move), so she's already a hair below the spawn, falling.
    CHECK(c.feet.x == c.spawn.x);
    CHECK(c.feet.z == c.spawn.z);
    CHECK(c.feet.y <= c.spawn.y);
    CHECK(c.feet.y > c.spawn.y - 0.01f);
    CHECK(std::holds_alternative<Air>(c.state));
}

TEST_CASE("turning on auto step-up before it's ported fails loudly, not silently") {   // Review Focus 5
    BoxWorld w = Floor();
    w.Add(Aabb::New({-1, 0, -2}, {1, 0.3f, -1.5f}));   // knee-high, just ahead (yaw 0 faces -z)
    Tuning t;
    t.autoStepUp = true;
    Controller c(t, {0, 0, 0}, 0.0f);
    c.state = Ground{};
    Input fwd;
    fwd.moveAxis = {0.0f, 1.0f};
    CHECK_THROWS_AS([&] { for (int i = 0; i < 120; ++i) c.Step(1.0f / 60.0f, fwd, w); }(), NotPorted);
}
