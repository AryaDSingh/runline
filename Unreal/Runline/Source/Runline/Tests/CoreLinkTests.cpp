#include "Misc/AutomationTest.h"
#include "RunlineCoreBridge.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCoreLinkTest, "Runline.Core.FallsAndLands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRunlineCoreLinkTest::RunTest(const FString& Parameters)
{
    // The core, as compiled by CMake for Unreal, runs a plain drop: 2 m onto a floor.
    runline::BoxWorld World;
    World.Add(runline::Aabb::New({-50.0f, -1.0f, -50.0f}, {50.0f, 0.0f, 50.0f}));
    runline::Controller C(runline::Tuning{}, {0.0f, 2.0f, 0.0f}, 0.0f);
    for (int I = 0; I < 120; ++I) C.Step(1.0f / 60.0f, runline::Input{}, World);
    TestTrue(TEXT("on the ground"), std::holds_alternative<runline::Ground>(C.state));
    TestEqual(TEXT("feet on the floor"), C.feet.y, 0.0f);
    return true;
}

#endif
