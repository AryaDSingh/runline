#include <doctest/doctest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include "runline/Tuning.h"
using namespace runline;

TEST_CASE("golden: every Tuning default matches faith_move, bit for bit, with nothing missing") {   // Review Focus 5
    std::ifstream in(std::string(RUNLINE_GOLDEN_DIR) + "/tuning.txt");
    REQUIRE_MESSAGE(in.good(), "tuning.txt missing; run cargo run --release in tools/golden");
    std::map<std::string, float> want;
    std::string name, value;
    while (in >> name >> value) want[name] = std::stof(value);
    REQUIRE(want.size() >= 150);

    std::map<std::string, float> got;
    for (const auto& [n, v] : TuningFields(Tuning{})) {
        CAPTURE(n);
        CHECK_MESSAGE(got.count(n) == 0, "listed twice");
        got[n] = v;
    }
    for (const auto& [n, v] : want) {
        CAPTURE(n);
        REQUIRE_MESSAGE(got.count(n) == 1, "missing from the C++ port");
        CHECK(std::bit_cast<std::uint32_t>(got[n]) == std::bit_cast<std::uint32_t>(v));
    }
    for (const auto& [n, v] : got) {
        CAPTURE(n);
        CHECK_MESSAGE(want.count(n) == 1, "not a field of faith_move's Tuning");
    }
}

TEST_CASE("SpeedForHeight is the launch speed to rise a height under gravity") {
    const Tuning t;
    CHECK(t.SpeedForHeight(0.0f) == 0.0f);
    CHECK(t.SpeedForHeight(-1.0f) == 0.0f);                       // never negative
    CHECK(t.SpeedForHeight(1.2f) == doctest::Approx(std::sqrt(2.0f * t.gravity * 1.2f)));
}
