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
