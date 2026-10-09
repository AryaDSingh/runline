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
