#include <doctest/doctest.h>
#include "runline/BoxWorld.h"
#include "runline/Move.h"
using namespace runline;

namespace {
const Body kStand{0.3f, 1.8f};
BoxWorld FloorAndWall() {   // floor top at y = 0, a long wall from x = 2
    BoxWorld w;
    w.Add(Aabb::New({-10, -1, -10}, {10, 0, 10})).Add(Aabb::New({2, 0, -10}, {3, 3, 10}));
    return w;
}
} // namespace

TEST_CASE("Body's box and reach") {
    const Aabb b = kStand.AabbAt({1, 0, 2});
    CHECK(b.min == Vec3{0.7f, 0, 1.7f});
    CHECK(b.max == Vec3{1.3f, 1.8f, 2.3f});
    CHECK(kStand.ReachToward({1, 0, 0}) == doctest::Approx(0.3f));
    CHECK(kStand.ReachToward({0.6f, 0, 0.8f}) == doctest::Approx(0.42f));
}

TEST_CASE("MoveAxis moves freely, or stops at the first thing in the way") {
    const auto w = FloorAndWall();
    Vec3 feet{0, 0, 0};
    CHECK_FALSE(MoveAxis(w, kStand, feet, 0, 1.0f).blocked);
    CHECK(feet.x == doctest::Approx(1.0f));
    const AxisHit h = MoveAxis(w, kStand, feet, 0, 4.0f);
    CHECK(h.blocked);
    CHECK(h.normal == Vec3{-1, 0, 0});
    CHECK(feet.x == doctest::Approx(1.7f));
    CHECK_FALSE(MoveAxis(w, kStand, feet, 2, 0.0f).blocked);   // a zero move does nothing
}

TEST_CASE("SlideMove slides along a wall it runs into at an angle") {
    const auto w = FloorAndWall();
    Vec3 feet{1.7f, 0, 0};                                     // touching the wall
    const auto ns = SlideMove(w, kStand, feet, {1, 0, 1});
    REQUIRE(ns.size() == 1);
    CHECK(ns[0] == Vec3{-1, 0, 0});
    CHECK(feet.x == doctest::Approx(1.7f));
    CHECK(feet.z == doctest::Approx(1.0f));
}

TEST_CASE("SlideMove into an inside corner stops dead with two normals") {   // Review Focus 2
    BoxWorld w;
    w.Add(Aabb::New({2, 0, -10}, {3, 3, 10})).Add(Aabb::New({-10, 0, 2}, {10, 3, 3}));
    Vec3 feet{1.7f, 0, 1.7f};
    const auto ns = SlideMove(w, kStand, feet, {1, 0, 1});
    REQUIRE(ns.size() == 2);
    CHECK(ns[0] == Vec3{-1, 0, 0});
    CHECK(ns[1] == Vec3{0, 0, -1});
    CHECK(feet == Vec3{1.7f, 0, 1.7f});
}

TEST_CASE("SlideMove ignores the vertical part of the move") {             // Review Focus 3
    const auto w = FloorAndWall();
    Vec3 feet{0, 0, 0};
    CHECK(SlideMove(w, kStand, feet, {1, 5, 0}).empty());
    CHECK(feet.x == doctest::Approx(1.0f));
    CHECK(feet.y == 0.0f);
}
