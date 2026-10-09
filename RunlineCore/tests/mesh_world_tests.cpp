#include <doctest/doctest.h>
#include "runline/MeshWorld.h"
using namespace runline;

namespace {
const Vec3 kHalf{0.3f, 0.9f, 0.3f};
MeshWorld Floor() {   // a 20 m floor at y = 0, as two triangles
    MeshWorld w;
    for (const Tri& t : MeshWorld::Quad({-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10})) w.tris.push_back(t);
    return w;
}
} // namespace

TEST_CASE("TriBoxOverlap is strict: resting on a triangle doesn't count") {
    const auto w = Floor();
    CHECK(TriBoxOverlap(w.tris[0], {-5, 0.5f, 5}, kHalf));       // cuts into the floor
    CHECK_FALSE(TriBoxOverlap(w.tris[0], {-5, 0.9f, 5}, kHalf)); // resting exactly on it
    CHECK_FALSE(TriBoxOverlap(w.tris[0], {-5, 3, 5}, kHalf));    // well above
}

TEST_CASE("falling onto a mesh floor stops just above it, facing up") {
    const auto w = Floor();
    const auto h = w.Sweep(kHalf, {0, 2, 0}, {0, -2, 0});
    REQUIRE(h);
    CHECK(h->normal == Vec3{0, 1, 0});                          // flipped to face the box
    CHECK(h->t == doctest::Approx(0.55f).epsilon(1e-3));
    CHECK(h->point.y == doctest::Approx(0.0f).epsilon(1e-3));
}

TEST_CASE("a box sunk less than SKIN is held, but can always get out") {     // Review Focus 4
    const auto w = Floor();
    const Vec3 sunk{0, 0.9f - 0.01f, 0};                        // 1 cm into the floor
    const auto down = w.Sweep(kHalf, sunk, {0, -0.5f, 0});
    REQUIRE(down);
    CHECK(down->t == 0.0f);
    CHECK(down->normal == Vec3{0, 1, 0});
    CHECK_FALSE(w.Sweep(kHalf, sunk, {0, 0.5f, 0}));             // up and out
    CHECK_FALSE(w.Sweep(kHalf, sunk, {1, 0, 0}));                // sideways
}

TEST_CASE("a zero-area triangle doesn't crash or block") {
    MeshWorld w;
    w.tris.push_back(Tri{Vec3{0, 1, 0}, Vec3{0, 1, 0}, Vec3{0, 1, 0}});
    CHECK_FALSE(w.Sweep(kHalf, {0, 1, 0}, {1, 0, 0}));
}

TEST_CASE("MeshWorld::Overlaps") {
    const auto w = Floor();
    CHECK(w.Overlaps(Aabb::New({-1, -0.5f, -1}, {1, 0.5f, 1})));
    CHECK_FALSE(w.Overlaps(Aabb::New({-1, 0, -1}, {1, 1, 1})));  // resting on top
    CHECK(TriBounds(w.tris[0]).min == Vec3{-10, 0, -10});
}
