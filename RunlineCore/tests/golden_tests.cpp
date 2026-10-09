#include "Golden.h"
#include "runline/Move.h"
#include "runline/Probes.h"
#include "runline/Locomotion.h"
#include "runline/Vault.h"
#include <cmath>
using namespace runline;

namespace {
void CheckSlides(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 21);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        Vec3 feet = golden::V(r, 2);
        const auto ns = SlideMove(w, Body{r[0], r[1]}, feet, golden::V(r, 5));
        rep.Vec(i, "feet", r, 8, feet);
        if (ns.size() != static_cast<size_t>(r[11])) {
            rep.Add(i, "count: want " + std::to_string(static_cast<int>(r[11])) + ", got " + std::to_string(ns.size()));
            continue;
        }
        for (size_t k = 0; k < ns.size(); ++k) rep.Vec(i, "n" + std::to_string(k + 1), r, 12 + 3 * k, ns[k]);
    }
    rep.Finish();
}

void CheckMoveAxis(const World& w, const char* file) {
    const auto rows = golden::Require(file, 500, 14);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        Vec3 feet = golden::V(r, 2);
        const AxisHit h = MoveAxis(w, Body{r[0], r[1]}, feet, static_cast<int>(r[5]), r[6]);
        rep.Vec(i, "feet", r, 7, feet);
        rep.Flag(i, "blocked", r[10], h.blocked);
        rep.Vec(i, "normal", r, 11, h.normal);
    }
    rep.Finish();
}

void CheckProbeWall(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 16);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto p = ProbeWall(w, Body{r[0], r[1]}, golden::V(r, 2), golden::V(r, 5), r[8], r[9], r[10]);
        rep.Flag(i, "found", r[11], p.has_value());
        if (!p || r[11] == 0.0f) continue;
        rep.Vec(i, "n", r, 12, p->n);
        rep.Float(i, "gap", r[15], p->gap);
    }
    rep.Finish();
}

void CheckTraceWall(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 18);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto h = TraceWall(w, Body{r[0], r[1]}, golden::V(r, 2), golden::V(r, 5), r[8], r[9]);
        rep.Flag(i, "found", r[10], h.has_value());
        if (!h || r[10] == 0.0f) continue;
        rep.Vec(i, "n", r, 11, h->n);
        rep.Vec(i, "at", r, 14, h->at);
        rep.Float(i, "gap", r[17], h->gap);
    }
    rep.Finish();
}

void CheckTraceBox(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 17);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto h = TraceBox(w, golden::V(r, 0), golden::V(r, 3), r[6], golden::V(r, 7));
        rep.Flag(i, "found", r[10], h.has_value());
        if (!h || r[10] == 0.0f) continue;
        rep.Vec(i, "at", r, 11, h->at);
        rep.Vec(i, "normal", r, 14, h->normal);
    }
    rep.Finish();
}

void CheckTopsBelow(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 15);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto ts = TopsBelow(w, golden::V(r, 0), r[3], r[4], r[5]);
        if (ts.size() != static_cast<size_t>(r[6])) {
            rep.Add(i, "count: want " + std::to_string(static_cast<int>(r[6])) + ", got " + std::to_string(ts.size()));
            continue;
        }
        for (size_t k = 0; k < ts.size(); ++k) rep.Float(i, "t" + std::to_string(k + 1), r[7 + k], ts[k]);
    }
    rep.Finish();
}

void CheckFindLedge(const World& w, const char* file) {
    const auto rows = golden::Require(file, 1000, 13);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto top = FindLedge(w, Body{r[0], r[1]}, golden::V(r, 2), golden::V(r, 5), r[8], r[9], r[10]);
        rep.Flag(i, "found", r[11], top.has_value());
        if (top && r[11] != 0.0f) rep.Float(i, "top", r[12], *top);
    }
    rep.Finish();
}
} // namespace

TEST_CASE("golden: Rust's f32 min, max and clamp, ties included") {   // Review Focus 1
    const auto rows = golden::Require("scalars.csv", 36, 6);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const float got[] = {FMin(r[0], r[1]), FMax(r[0], r[1]), FClamp(r[0], 0.0f, 1.0f), FClamp(r[0], -0.0f, 0.0f)};
        const char* names[] = {"min", "max", "clamp01", "clampz"};
        for (int k = 0; k < 4; ++k)
            if (!golden::Same(got[k], r[2 + k]))
                rep.Add(i, std::string(names[k]) + ": want " + std::to_string(r[2 + k]) + ", got " + std::to_string(got[k]));
    }
    rep.Finish();
}

TEST_CASE("golden: BoxWorld::Sweep matches faith_move") {
    const BoxWorld w = golden::BoxScene();
    const auto rows = golden::Require("sweeps.csv", 1000, 17);   // Review Focus 4 (M0)
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        golden::CompareHit(rep, i, w.Sweep(golden::V(r, 0), golden::V(r, 3), golden::V(r, 6)), r, 9);
    }
    rep.Finish();
}

TEST_CASE("golden data records the faith-runner commit it came from") {
    std::ifstream in(std::string(RUNLINE_GOLDEN_DIR) + "/REFERENCE");
    std::string line;
    std::getline(in, line);
    REQUIRE_MESSAGE(line.rfind("faith-runner ", 0) == 0, "REFERENCE missing; run cargo run in tools/golden");
    CHECK(line.size() >= 13 + 40);                    // a full 40-hex commit SHA
    CHECK(line.find("dirty") == std::string::npos);   // made from a clean checkout
}

TEST_CASE("golden: SlideMove matches faith_move (boxes)") { CheckSlides(golden::BoxScene(), "slides.csv"); }
TEST_CASE("golden: MoveAxis matches faith_move (boxes)") { CheckMoveAxis(golden::BoxScene(), "move_axis.csv"); }
TEST_CASE("golden: ProbeWall matches faith_move (boxes)") { CheckProbeWall(golden::BoxScene(), "probe_wall.csv"); }
TEST_CASE("golden: TraceWall matches faith_move (boxes)") { CheckTraceWall(golden::BoxScene(), "trace_wall.csv"); }
TEST_CASE("golden: TraceBox matches faith_move (boxes)") { CheckTraceBox(golden::BoxScene(), "trace_box.csv"); }
TEST_CASE("golden: TopsBelow matches faith_move (boxes)") { CheckTopsBelow(golden::BoxScene(), "tops_below.csv"); }
TEST_CASE("golden: FindLedge matches faith_move (boxes)") { CheckFindLedge(golden::BoxScene(), "find_ledge.csv"); }

TEST_CASE("golden: MeshWorld::Sweep matches faith_move") {
    const MeshWorld w = golden::MeshScene();
    const auto rows = golden::Require("mesh_sweeps.csv", 1000, 17);
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        golden::CompareHit(rep, i, w.Sweep(golden::V(r, 0), golden::V(r, 3), golden::V(r, 6)), r, 9);
    }
    rep.Finish();
}
TEST_CASE("golden: SlideMove matches faith_move (mesh)") { CheckSlides(golden::MeshScene(), "mesh_slides.csv"); }
TEST_CASE("golden: TraceWall matches faith_move (mesh)") { CheckTraceWall(golden::MeshScene(), "mesh_trace_wall.csv"); }
TEST_CASE("golden: FindLedge matches faith_move (mesh)") { CheckFindLedge(golden::MeshScene(), "mesh_find_ledge.csv"); }

TEST_CASE("golden: locomotion curves match faith_move") {
    const auto rows = golden::Require("curves.csv", 400, 3);
    const Locomotion loco;
    const Curve speed{SPEED_CURVE};
    golden::Report rep;
    size_t point = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r[0] == 0.0f) {
            REQUIRE(point < loco.accelCurve.points.size());
            rep.Float(i, "point.x", r[1], loco.accelCurve.points[point].first);
            rep.Float(i, "point.y", r[2], loco.accelCurve.points[point].second);
            ++point;
        } else {
            rep.Float(i, r[0] == 1.0f ? "speed.eval" : "accel.eval", r[2], (r[0] == 1.0f ? speed : loco.accelCurve).Eval(r[1]));
        }
    }
    CHECK(point == loco.accelCurve.points.size());
    rep.Finish();
}

TEST_CASE("golden: sprint, walk and controller acceleration match faith_move") {
    const auto rows = golden::Require("loco_accel.csv", 3000, 27);
    const Locomotion loco;
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        RunState st{r[1], r[2], r[3]};
        const RunInput in{golden::V(r, 4), golden::V(r, 7), r[10], r[11], r[12]};
        Vec3 vel = golden::V(r, 13);
        const float dt = r[16];
        const bool falling = r[17] != 0.0f;
        const int kind = static_cast<int>(r[0]);
        const Vec3 a = kind == 0 ? loco.SprintAccel(st, in, vel, dt, falling)
                     : kind == 1 ? loco.WalkAccel(st, in, vel, dt, falling)
                                 : loco.ControllerAccel(st, in, vel, dt);
        rep.Vec(i, "accel", r, 18, a);
        rep.Float(i, "energy", r[21], st.energy);
        rep.Float(i, "accelTime", r[22], st.accelTime);
        rep.Float(i, "stopping", r[23], st.stopping);
        rep.Vec(i, "vel", r, 24, vel);
    }
    rep.Finish();
}

TEST_CASE("golden: CalcVelocity matches faith_move, braking and dt = 0 included") {   // Review Focus 4
    const auto rows = golden::Require("loco_velocity.csv", 2000, 12);
    const Locomotion loco;
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        rep.Vec(i, "v", r, 9, loco.CalcVelocity(golden::V(r, 0), golden::V(r, 3), r[6], r[7], r[8]));
    }
    rep.Finish();
}

TEST_CASE("golden: VAULT_TYPES matches faith_move's table bit for bit") {
    const auto rows = golden::Require("vault_types.csv", 6, 17);
    REQUIRE(rows.size() == VAULT_TYPES.size());
    golden::Report rep;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const VaultType& t = VAULT_TYPES[i];
        rep.Flag(i, "onto", r[0], t.onto);
        const float got[] = {t.minHeight, t.maxHeight, t.minSpeedZ, t.maxSpeedZ, t.maxMomentum, t.maxDistanceTime, t.clampSpeedMin,
                             t.clampSpeedMax, t.speedAddition, t.timeUp, t.timeOver, t.timeDown, t.handplant.first, t.handplant.second,
                             t.ledge.first, t.ledge.second};
        for (int k = 0; k < 16; ++k) rep.Float(i, "field " + std::to_string(k + 1), r[1 + k], got[k]);
    }
    rep.Finish();
}
