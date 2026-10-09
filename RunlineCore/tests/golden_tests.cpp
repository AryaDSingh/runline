#include "Golden.h"
#include <cmath>
using namespace runline;

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
