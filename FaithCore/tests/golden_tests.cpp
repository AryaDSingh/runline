#include <doctest/doctest.h>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "faith/BoxWorld.h"
using namespace faith;

namespace {
std::vector<std::vector<float>> ReadCsv(const std::string& name) {
    std::ifstream in(std::string(FAITH_GOLDEN_DIR) + "/" + name);
    std::vector<std::vector<float>> rows;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::vector<float> row;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) row.push_back(std::stof(cell));
        rows.push_back(row);
    }
    return rows;
}

// The scene is written by tools/golden, so it can't drift from the Rust side.
BoxWorld LoadScene() {
    const auto rows = ReadCsv("scene.csv");
    REQUIRE_MESSAGE(!rows.empty(), "scene.csv missing; run cargo run in tools/golden");
    BoxWorld w;
    for (const auto& r : rows) {
        REQUIRE(r.size() == 6);
        w.Add(Aabb::New({r[0], r[1], r[2]}, {r[3], r[4], r[5]}));
    }
    return w;
}

const char* kFields[] = {"t", "nx", "ny", "nz", "px", "py", "pz"};
} // namespace

TEST_CASE("golden: BoxWorld::Sweep matches faith_move") {
    const BoxWorld w = LoadScene();
    const auto rows = ReadCsv("sweeps.csv");
    REQUIRE_MESSAGE(rows.size() >= 1000, "sweeps.csv missing or empty; run cargo run in tools/golden");  // Review Focus 4
    int mismatches = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        REQUIRE(r.size() == 17);
        const auto h = w.Sweep({r[0], r[1], r[2]}, {r[3], r[4], r[5]}, {r[6], r[7], r[8]});
        const bool want = r[9] != 0.0f;
        std::string diff;
        if (h.has_value() != want) {
            diff = std::string("hit: want ") + (want ? "1" : "0") + ", got " + (h ? "1" : "0");
        } else if (want) {
            const float got[] = {h->t, h->normal.x, h->normal.y, h->normal.z,
                                 h->point.x, h->point.y, h->point.z};
            for (int k = 0; k < 7 && diff.empty(); ++k) {
                const float exp = r[10 + k];
                const bool exact = k >= 1 && k <= 3;   // normals must match exactly
                if (exact ? got[k] != exp : std::fabs(got[k] - exp) > 1e-5f)
                    diff = std::string(kFields[k]) + ": want " + std::to_string(exp) + ", got " + std::to_string(got[k]);
            }
        }
        if (!diff.empty() && ++mismatches <= 10)
            MESSAGE("case " << i << " (csv line " << i + 2 << ") differs in " << diff);
    }
    CHECK(mismatches == 0);
}
