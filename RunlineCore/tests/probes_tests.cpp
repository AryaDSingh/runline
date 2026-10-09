#include <doctest/doctest.h>
#include "runline/BoxWorld.h"
#include "runline/Probes.h"
using namespace runline;

namespace {
const Body kStand{0.3f, 1.8f};
// Floor top at y = 0; a wall from x = 2; a 1.2 m block at x -4..-2, z 2..4.
BoxWorld Scene() {
    BoxWorld w;
    w.Add(Aabb::New({-10, -1, -10}, {10, 0, 10}))
     .Add(Aabb::New({2, 0, -10}, {3, 3, 10}))
     .Add(Aabb::New({-4, 0, 2}, {-2, 1.2f, 4}));
    return w;
}
} // namespace

TEST_CASE("ClosestOnSegment clamps to the ends and survives a zero-length segment") {
    const auto c = ClosestOnSegment({0, 0, 0}, {10, 0, 0}, {3, 5, 0});
    CHECK(c.point == Vec3{3, 0, 0});
    CHECK(c.along == doctest::Approx(3.0f));
    CHECK(ClosestOnSegment({0, 0, 0}, {10, 0, 0}, {-2, 1, 0}).along == 0.0f);
    CHECK(ClosestOnSegment({1, 1, 1}, {1, 1, 1}, {5, 5, 5}).point == Vec3{1, 1, 1});
}

TEST_CASE("ProbeWall finds the wall ahead and the gap to it") {
    const auto w = Scene();
    const auto p = ProbeWall(w, kStand, {1.5f, 0, 0}, {1, 0, 0}, 1.0f, 0.0f, 1.8f);
    REQUIRE(p);
    CHECK(p->n == Vec3{-1, 0, 0});
    CHECK(p->gap == doctest::Approx(0.2f));
    CHECK_FALSE(ProbeWall(w, kStand, {1.5f, 0, 0}, {-1, 0, 0}, 1.0f, 0.0f, 1.8f));   // nothing behind
}

TEST_CASE("probes with no direction or no reach find nothing") {                     // Review Focus 5
    const auto w = Scene();
    CHECK_FALSE(ProbeWall(w, kStand, {1.5f, 0, 0}, {0, 1, 0}, 1.0f, 0.0f, 1.8f));      // straight up: no horizontal dir
    CHECK_FALSE(TraceWall(w, kStand, {1.5f, 0, 0}, {0, 0, 0}, 1.0f, 1.0f));
    CHECK_FALSE(TraceWall(w, kStand, {1.5f, 0, 0}, {1, 0, 0}, 0.0f, 1.0f));
    CHECK_FALSE(TraceBox(w, {0, 1, 0}, {0, 0, 0}, 1.0f, Vec3::Splat(0.05f)));
    CHECK_FALSE(TraceBox(w, {0, 1, 0}, {1, 0, 0}, 0.0f, Vec3::Splat(0.05f)));
}

TEST_CASE("TraceWall gives the face point straight out from the body") {
    const auto w = Scene();
    const auto h = TraceWall(w, kStand, {1.5f, 0, 0}, {1, 0, 0}, 1.0f, 1.0f);
    REQUIRE(h);
    CHECK(h->n == Vec3{-1, 0, 0});
    CHECK(h->at.x == doctest::Approx(2.0f));
    CHECK(h->at.y == doctest::Approx(1.0f));
    CHECK(h->gap == doctest::Approx(0.2f));
}

TEST_CASE("TopsBelow lists every walkable top under a column, highest first") {
    const auto w = Scene();
    const auto tops = TopsBelow(w, {-3, 0, 3}, 0.01f, 7.0f, -1.0f);
    REQUIRE(tops.size() == 2);
    CHECK(tops[0] == doctest::Approx(1.2f));
    CHECK(tops[1] == doctest::Approx(0.0f));
    CHECK(TopsBelow(w, {-3, 0, 3}, 0.01f, 0.5f, 2.0f).empty());   // y_lo above y_hi: nothing
    CHECK(ColumnTop(w, {0, 0, 0}, -1.0f, 5.0f) == doctest::Approx(0.0f));
    CHECK(ColumnTop(w, {20, 0, 0}, -1.0f, 5.0f) == -1.0f);        // nothing there: y0
}

TEST_CASE("FindLedge finds a climbable top, and refuses one with no room for the hands") {
    auto w = Scene();
    const auto top = FindLedge(w, kStand, {-1.7f, 0, 3}, {1, 0, 0}, 0.5f, 2.5f, 1.22f);
    REQUIRE(top);
    CHECK(*top == doctest::Approx(1.2f));
    // Something on our side of the wall, where the hands go (not over the ledge itself, or its
    // own top would be a ledge).
    w.Add(Aabb::New({-1.9f, 1.3f, 2}, {-1.4f, 1.4f, 4}));
    CHECK_FALSE(FindLedge(w, kStand, {-1.7f, 0, 3}, {1, 0, 0}, 0.5f, 2.5f, 1.22f));
}

TEST_CASE("FindLedge needs 35 cm of clearance above the lip for the hands") {
    auto w = Scene();
    // 30 cm above the block's 1.2 m top, on our side: inside the 35 cm the hands need.
    w.Add(Aabb::New({-1.9f, 1.5f, 2}, {-1.4f, 1.6f, 4}));
    CHECK_FALSE(FindLedge(w, kStand, {-1.7f, 0, 3}, {1, 0, 0}, 0.5f, 2.5f, 1.22f));
}
