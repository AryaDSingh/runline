#include <doctest/doctest.h>
#include "runline/Vec3.h"
#include <cmath>
using runline::Vec3;
using runline::FMin;
using runline::FMax;
using runline::FClamp;

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

TEST_CASE("Vec3 dot, cross, length and normalize, in glam's operation order") {
    Vec3 a{1, 2, 3}, b{4, -5, 6};
    CHECK(a.Dot(b) == 12.0f);                      // 4 - 10 + 18
    CHECK(a.Cross(b) == Vec3{27, 6, -13});         // (2*6 - -5*3, 3*4 - 6*1, 1*-5 - 4*2)
    CHECK(Vec3{3, 4, 0}.Length() == 5.0f);
    CHECK(Vec3{3, 4, 0}.LengthSquared() == 25.0f);
    CHECK(Vec3{0, 0, 2}.Normalize() == Vec3{0, 0, 1});
    CHECK(Vec3{0, 0, 2}.NormalizeOrZero() == Vec3::UnitZ());
    CHECK(Vec3{}.NormalizeOrZero() == Vec3::Zero());   // no direction: zero, not NaN
    CHECK(-a == Vec3{-1, -2, -3});
    CHECK(b / 2.0f == Vec3{2, -2.5f, 3});
    Vec3 c = a; c += b; c -= a; c *= 2.0f;
    CHECK(c == Vec3{8, -10, 12});
}

TEST_CASE("ties go to the second argument, as in glam and Rust") {     // Review Focus 1
    CHECK_FALSE(std::signbit(FMax(-0.0f, 0.0f)));                      // (-0.0f32).max(0.0) is +0
    CHECK_FALSE(std::signbit(Vec3::Min(Vec3{-0.0f, 1, 1}, Vec3{0.0f, 1, 1}).x));
    CHECK(FClamp(5.0f, 0.0f, 1.0f) == 1.0f);
    CHECK(FClamp(-5.0f, 0.0f, 1.0f) == 0.0f);
    CHECK(FMin(1.0f, 2.0f) == 1.0f);
}
