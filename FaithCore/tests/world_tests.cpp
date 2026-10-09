#include <doctest/doctest.h>
#include "faith/BoxWorld.h"
using namespace faith;

static BoxWorld Floor() {  // a 20 m slab whose top is at y = 0
    BoxWorld w; w.Add(Aabb::New({-10, -1, -10}, {10, 0, 10})); return w;
}
static const Vec3 kHalf{0.3f, 0.9f, 0.3f};

TEST_CASE("Aabb::New orders corners; Overlaps is strict") {
    Aabb a = Aabb::New({1, 1, 1}, {0, 0, 0});
    CHECK(a.min == Vec3{0, 0, 0}); CHECK(a.max == Vec3{1, 1, 1});
    CHECK_FALSE(a.Overlaps(Aabb::New({1, 0, 0}, {2, 1, 1})));      // touching face
    CHECK(a.Overlaps(Aabb::New({0.5f, 0.5f, 0.5f}, {2, 2, 2})));
}

TEST_CASE("falling onto the floor hits its top") {
    auto w = Floor();
    auto h = w.Sweep(kHalf, {0, 2, 0}, {0, -2, 0});   // feet start at 1.1, floor at 0
    REQUIRE(h);
    CHECK(h->t == doctest::Approx(1.1f / 2.0f));
    CHECK(h->normal == Vec3{0, 1, 0});
    CHECK(h->point.y == doctest::Approx(0.0f));
}

TEST_CASE("touching sideways passes; touching ahead hits at t = 0") {   // Review Focus 1
    auto w = Floor();
    Vec3 standing{0, 0.9f, 0};                         // feet exactly on the floor
    CHECK_FALSE(w.Sweep(kHalf, standing, {1, 0, 0}));   // slide along it
    auto h = w.Sweep(kHalf, standing, {0, -0.5f, 0});   // push into it
    REQUIRE(h); CHECK(h->t == doctest::Approx(0.0f));
}

TEST_CASE("pure axis moves use the zero-delta branch") {               // Review Focus 2
    BoxWorld w; w.Add(Aabb::New({2, 0, -1}, {3, 3, 1}));                 // wall ahead on +x
    auto h = w.Sweep(kHalf, {0, 1, 0}, {4, 0, 0});
    REQUIRE(h);
    CHECK(h->t == doctest::Approx(1.7f / 4.0f));
    CHECK(h->normal == Vec3{-1, 0, 0});
}

TEST_CASE("a box starting inside a solid ignores it") {                 // Review Focus 3
    BoxWorld w; w.Add(Aabb::New({-1, 0, -1}, {1, 3, 1}));
    CHECK_FALSE(w.Sweep(kHalf, {0, 1, 0}, {0.5f, 0, 0}));
}

TEST_CASE("zero delta never hits") {                                     // Review Focus 5
    auto w = Floor();
    CHECK_FALSE(w.Sweep(kHalf, {0, 0.9f, 0}, {0, 0, 0}));
}

TEST_CASE("the nearest of several boxes wins") {
    BoxWorld w;
    w.Add(Aabb::New({5, 0, -1}, {6, 3, 1})).Add(Aabb::New({2, 0, -1}, {3, 3, 1}));
    auto h = w.Sweep(kHalf, {0, 1, 0}, {10, 0, 0});
    REQUIRE(h); CHECK(h->t == doctest::Approx(0.17f));
}

TEST_CASE("walking across a seam between flush floor boxes does not snag") {
    BoxWorld w;   // greybox floors are built from flush boxes like this
    w.Add(Aabb::New({-10, -1, -10}, {0, 0, 10})).Add(Aabb::New({0, -1, -10}, {10, 0, 10}));
    CHECK_FALSE(w.Sweep(kHalf, {-1, 0.9f, 0}, {2, 0, 0}));
}

TEST_CASE("an exact corner tie picks the first axis (x), as faith_move does") {
    BoxWorld w; w.Add(Aabb::New({1, 0, 1}, {2, 3, 2}));
    auto h = w.Sweep({0.5f, 0.9f, 0.5f}, {0, 1, 0}, {1, 0, 1});
    REQUIRE(h);
    CHECK(h->normal == Vec3{-1, 0, 0});   // `se > enterS` is strict: x keeps the tie
    CHECK(h->t == doctest::Approx(0.5f));
}

TEST_CASE("Overlaps and IsFree") {
    auto w = Floor();
    CHECK(w.Overlaps(Aabb::New({0, -0.5f, 0}, {1, 0.5f, 1})));
    CHECK(w.IsFree(Aabb::New({0, 0, 0}, {1, 1, 1})));   // resting on top only
}

TEST_CASE("a box sunk inside the TOUCH band still crosses a flush seam") {
    BoxWorld w;   // the zero-delta branch must use TOUCH, not the raw faces
    w.Add(Aabb::New({-10, -1, -10}, {0, 0, 10})).Add(Aabb::New({0, -1, -10}, {10, 0, 10}));
    CHECK_FALSE(w.Sweep(kHalf, {-1, 0.9f - 5e-5f, 0}, {2, 0, 0}));
}

TEST_CASE("a move ending inside the TOUCH band of a face does not hit") {
    auto w = Floor();   // ends 5e-5 into the floor: still only touching (enterS >= 1)
    CHECK_FALSE(w.Sweep(kHalf, {0, 1.4f, 0}, {0, -0.50005f, 0}));
}
