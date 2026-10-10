#include "Misc/AutomationTest.h"
#include "RunlineUnits.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RunlineUnits;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineUnitsAxes, "Runline.Units.Axes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineUnitsAxes::RunTest(const FString&)
{
    // Right stays right, up stays up, the core's forward (-Z at yaw 0) is Unreal's +X.
    TestEqual(TEXT("core right -> UE +Y"), ToUnrealDirection({1, 0, 0}), FVector(0, 1, 0));
    TestEqual(TEXT("core up -> UE +Z"), ToUnrealDirection({0, 1, 0}), FVector(0, 0, 1));
    TestEqual(TEXT("core forward -> UE +X"), ToUnrealDirection({0, 0, -1}), FVector(1, 0, 0));
    TestEqual(TEXT("1 m -> 100 cm"), ToUnrealPosition({1, 2, 3}), FVector(-300, 100, 200));
    // Yaw: core +90 deg turns left, so it faces UE -Y.
    const FVector Fwd = ToUnrealRotator(UE_HALF_PI, 0.0f).Vector();
    TestTrue(TEXT("core yaw +pi/2 faces UE -Y"), Fwd.Equals(FVector(0, -1, 0), 1e-6));
    // Pitch: positive looks up in both.
    TestTrue(TEXT("pitch up"), ToUnrealRotator(0.0f, 0.5f).Vector().Z > 0.0);
    // The rotator's forward is the converted core forward for any yaw/pitch.
    for (float Yaw : {-2.5f, -1.0f, 0.0f, 0.3f, 1.7f, 3.0f})
    {
        for (float Pitch : {-1.2f, 0.0f, 0.8f})
        {
            const runline::Vec3 F{-std::sin(Yaw) * std::cos(Pitch), std::sin(Pitch), -std::cos(Yaw) * std::cos(Pitch)};
            TestTrue(FString::Printf(TEXT("forward at yaw %f pitch %f"), Yaw, Pitch),
                ToUnrealRotator(Yaw, Pitch).Vector().Equals(ToUnrealDirection(F), 1e-5));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineUnitsRoundTrip, "Runline.Units.RoundTripIsExact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineUnitsRoundTrip::RunTest(const FString&)
{
    // Bit-exact both ways (float * 100 and / 100 in double recover the float).
    const runline::Vec3 Samples[] = {{2.3f, -1.5f, -146.0f}, {0.1f, 1e-4f, -0.3f}, {-200.0f, -31.0f, 120.0f}, {1.12f, 0.64f, 5.0f}};
    for (const runline::Vec3& P : Samples)
    {
        TestTrue(TEXT("position round trip"), ToCorePosition(ToUnrealPosition(P)) == P);
        TestTrue(TEXT("extent round trip"), ToCoreExtent(ToUnrealExtent(P)) == P);
    }
    const runline::Aabb B = runline::Aabb::New({-12.0f, -4.2f, -146.5f}, {12.0f, -2.6f, -146.0f});
    const runline::Aabb R = ToCoreBox(ToUnrealBox(B));
    TestTrue(TEXT("box round trip"), R.min == B.min && R.max == B.max);
    TestTrue(TEXT("UE box is valid (min < max)"), ToUnrealBox(B).Min.X < ToUnrealBox(B).Max.X);
    // And through a centre and extent, as a UBoxComponent stores it.
    const FBox U = ToUnrealBox(B);
    const runline::Aabb C = ToCoreBox(FBox(U.GetCenter() - U.GetExtent(), U.GetCenter() + U.GetExtent()));
    TestTrue(TEXT("box round trip through centre and extent"), C.min == B.min && C.max == B.max);
    return true;
}
#endif
