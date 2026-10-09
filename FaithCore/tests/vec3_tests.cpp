#include <doctest/doctest.h>
#include "faith/Vec3.h"
using faith::Vec3;

TEST_CASE("Vec3 arithmetic and indexing") {
    Vec3 a{1, 2, 3}, b{4, -5, 6};
    CHECK(a + b == Vec3{5, -3, 9});
    CHECK(b - a == Vec3{3, -7, 3});
    CHECK(a * 2.0f == Vec3{2, 4, 6});
    CHECK(a[0] == 1); CHECK(a[1] == 2); CHECK(a[2] == 3);
    Vec3 c = a; c[1] = 9; CHECK(c == Vec3{1, 9, 3});
}

TEST_CASE("Vec3 min, max, clamp, splat") {
    Vec3 a{1, 5, -2}, b{3, 2, -4};
    CHECK(Vec3::Min(a, b) == Vec3{1, 2, -4});
    CHECK(Vec3::Max(a, b) == Vec3{3, 5, -2});
    CHECK(Vec3{5, -5, 0.5f}.Clamp(Vec3{0, 0, 0}, Vec3{1, 1, 1}) == Vec3{1, 0, 0.5f});
    CHECK(Vec3::Splat(2) == Vec3{2, 2, 2});
}
