#include "Golden.h"
#include "runline/Move.h"
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
