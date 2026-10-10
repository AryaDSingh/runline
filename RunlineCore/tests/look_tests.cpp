#include "Golden.h"
#include "runline/Controller.h"
#include "runline/Look.h"
#include <cmath>
using namespace runline;

namespace {

constexpr float DT = 1.0f / 60.0f;
float Uu(int r) { return static_cast<float>(r) / 65536.0f * kTau; }

/// tools/golden/src/look.rs's `state`: a controller in the state `code`.
Controller StateFor(int code, float param) {
    Controller c(Tuning{}, Vec3{}, 0.0f);
    const auto tr = [param](TraverseKind kind) { return Traverse{kind, Vec3{}, Vec3::UnitY(), param, 0.5f, 0.0f, Vec3{}}; };
    switch (code) {
        case 0: c.state = Ground{}; break;
        case 1: c.crouched = true; c.state = Ground{}; break;
        case 2: c.state = Air{}; break;
        case 3: c.turnedInAir = true; c.state = Air{}; break;
        case 4: c.state = Slide{param}; break;
        case 5: c.state = Roll{param}; break;
        case 6: c.state = Stunned{param}; break;
        case 7: c.state = WallRun{{1, 0, 0}, param}; break;
        case 8: c.state = WallClimb{{0, 0, 1}, param}; break;
        case 9: c.state = WallClimbTurned{{0, 0, 1}, param}; break;
        case 10: c.state = LedgeHang{{0, 0, 1}, 1.8f, false}; break;
        case 11: c.state = LedgeHang{{0, 0, 1}, 1.8f, true}; break;
        case 12: c.state = GrabTransfer{param, Vec3{}, Vec3::UnitY(), 0.5f, {0, 0, 1}, 1.8f}; break;
        case 13: c.state = tr(TraverseKind::Vault); break;
        case 14: c.state = tr(TraverseKind::Mantle); break;
        case 15: c.state = tr(TraverseKind::PullUp); break;
        case 16: c.state = tr(TraverseKind::SpringBoard); break;
        case 17: c.state = LayOnGround{param, std::nullopt, false}; break;
        default: FAIL("unknown state code " << code);
    }
    return c;
}

} // namespace

TEST_CASE("golden: look limits, the LookLimiter and the swan neck match faith_move") {
    const auto rows = golden::Require("look_limits.csv", 1000, 8);
    golden::Report rep;
    // Runs are consecutive rows with the same (kind, code, param); replay each from its first row.
    size_t i = 0;
    while (i < rows.size()) {
        const auto& r = rows[i];
        const int kind = static_cast<int>(r[0]), code = static_cast<int>(r[1]);
        const float param = r[2];
        if (kind == 0) {
            const LookLimit l = LookLimitFor(StateFor(code, param));
            rep.Float(i, "pitch_min", r[4], l.pitchMin);
            rep.Float(i, "pitch_max", r[5], l.pitchMax);
            rep.Flag(i, "has_yaw", r[6], l.yaw.has_value());
            if (l.yaw) rep.Float(i, "yaw_half", r[7], *l.yaw);
            ++i;
        } else if (kind == 1) {
            const float run = param;
            Controller c = StateFor(code, r[6]);   // c: the state's own param
            c.yaw = 0.3f;
            c.slideYaw = -0.4f;
            LookLimiter l;
            l.Apply(c, DT);
            for (int frame = 1; frame <= 90; ++frame, ++i) {
                if (frame == 1) {
                    c.yaw += run == 2.0f ? -2.5f : 2.5f;
                    c.pitch = run == 2.0f ? 1.4f : -1.4f;
                }
                if (run == 3.0f && frame == 30) {
                    c.state = LedgeHang{{0, 0, 1}, 1.8f, false};
                    c.pitch = -1.3f;
                }
                l.Apply(c, DT);
                REQUIRE(i < rows.size());
                rep.Float(i, "limited pitch", rows[i][4], c.pitch);
                rep.Float(i, "limited yaw", rows[i][5], c.yaw);
            }
        } else {
            Controller c = StateFor(code, param);
            SwanNeck s;
            const size_t start = i;
            while (i < rows.size() && rows[i][0] == 2.0f && rows[i][1] == r[1] && rows[i][2] == r[2] && rows[i][3] == static_cast<float>(i - start)) {
                c.pitch = rows[i][6];
                const auto [f, d] = s.Update(c, DT);
                rep.Float(i, "swan forward", rows[i][4], f);
                rep.Float(i, "swan down", rows[i][5], d);
                ++i;
            }
        }
    }
    rep.Finish();
}

// look.rs's own tests.
TEST_CASE("a ledge hang limits looking down") {
    Controller c(Tuning{}, Vec3{}, 0.0f);
    c.state = LedgeHang{{0, 0, 1}, 1.8f, false};
    c.pitch = -1.2f;
    LookLimiter l;
    for (int k = 0; k < 60; ++k) l.Apply(c, DT);
    CHECK(std::fabs(c.pitch - Uu(-3200)) < 1e-3f);
}

TEST_CASE("a wallrun limits yaw around the yaw it started with") {
    Controller c(Tuning{}, Vec3{}, 0.0f);
    c.state = WallRun{{1, 0, 0}, 0.0f};
    LookLimiter l;
    l.Apply(c, 0.01f);
    c.yaw = 2.5f;   // way past +-90 degrees
    for (int k = 0; k < 60; ++k) l.Apply(c, DT);
    CHECK(std::fabs(c.yaw - Uu(16384)) < 1e-3f);
}

TEST_CASE("the swan neck moves the camera forward and down when looking down") {
    Controller c(Tuning{}, Vec3{}, 0.0f);
    c.state = Ground{};
    SwanNeck s;
    c.pitch = -0.1f;   // under 15 degrees: nothing
    std::pair<float, float> fd{};
    for (int k = 0; k < 120; ++k) fd = s.Update(c, DT);
    CHECK(fd.first < 1e-3f);
    c.pitch = -1.45f;   // nearly straight down
    for (int k = 0; k < 120; ++k) fd = s.Update(c, DT);
    // TdSwanNeck at 83 degrees down: forward 23 cm, down 16 cm.
    CHECK(std::fabs(fd.first - 0.233f) < 0.005f);
    CHECK(std::fabs(fd.second - 0.156f) < 0.005f);
}

TEST_CASE("a skill roll turns the swan neck off after 0.2 s") {
    Controller c(Tuning{}, Vec3{}, 0.0f);
    c.pitch = -1.2f;
    SwanNeck s;
    std::pair<float, float> fd{};
    for (int k = 0; k < 60; ++k) fd = s.Update(c, DT);
    const float f0 = fd.first;
    CHECK(f0 > 0.1f);
    c.state = Roll{0.1f};
    fd = s.Update(c, DT);
    CHECK(std::fabs(fd.first - f0) < 1e-3f);   // still on before 0.2 s
    c.state = Roll{0.3f};
    for (int k = 0; k < 60; ++k) fd = s.Update(c, DT);
    CHECK(fd.first < 1e-3f);
    CHECK(fd.second < 1e-3f);
}

TEST_CASE("legs tucked in the air (coil) limit the look to TdMove_Coil's range") {
    // Not in the golden (Controller::coiled is private in faith_move): checked against the table.
    // TdMove_Coil.CanDoMove: only out of a jump, moving forward: run, jump, then tuck.
    BoxWorld w;
    w.Add(Aabb::New({-50, -1, -50}, {50, 0, 50}));
    Controller c(Tuning{}, Vec3{}, 0.0f);
    Input run;
    run.moveAxis = {0, 1};
    for (int k = 0; k < 60; ++k) c.Step(DT, run, w);
    Input jump = run;
    jump.jumpPressed = jump.jumpHeld = true;
    c.Step(DT, jump, w);
    Input tuck = run;
    tuck.crouchPressed = tuck.crouchHeld = true;
    for (int k = 0; k < 5 && !c.IsCoiled(); ++k) c.Step(DT, tuck, w);
    REQUIRE(c.IsCoiled());
    const LookLimit l = LookLimitFor(c);
    CHECK(l.pitchMin == FMax(Uu(-5000), -1.48f));
    CHECK(l.pitchMax == FMin(Uu(30000), 1.48f));
    CHECK_FALSE(l.yaw.has_value());
}
