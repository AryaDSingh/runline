#include "Misc/AutomationTest.h"
#include "RunlineTimer.h"

#if WITH_DEV_AUTOMATION_TESTS

// faith-runner's time trial (src/main.rs): the clock starts when you leave the start region on the
// ground, stops at the finish, and a respawn at the start resets it.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineTimerStart, "Runline.Timer.StartsLeavingTheStartOnGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineTimerStart::RunTest(const FString&)
{
	FRunlineTimer T;
	T.Update(0.1f, /*bInStart*/ true, /*bOnGround*/ true, /*bInFinish*/ false, /*Checkpoint*/ 0);
	TestFalse(TEXT("not while still in the start"), T.Running.IsSet());
	T.Update(0.1f, false, /*bOnGround*/ false, false, 0);
	TestFalse(TEXT("not in the air (jumping out of the start)"), T.Running.IsSet());
	T.Update(0.1f, false, true, false, 0);
	TestTrue(TEXT("on the ground outside the start"), T.Running.IsSet());
	TestEqual(TEXT("counts that frame"), T.Running.GetValue(), 0.1f);
	T.Update(0.25f, false, false, false, 0);
	TestEqual(TEXT("keeps counting in the air"), T.Running.GetValue(), 0.35f);
	FRunlineTimer Later;
	Later.Update(0.1f, false, true, false, /*Checkpoint*/ 2);
	TestFalse(TEXT("doesn't start from a later checkpoint"), Later.Running.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineTimerFinish, "Runline.Timer.StopsAtTheFinishAndKeepsTheBest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineTimerFinish::RunTest(const FString&)
{
	FRunlineTimer T;
	T.Update(0.5f, false, true, false, 0);
	TestFalse(TEXT("not finished yet"), T.Update(1.0f, false, true, false, 3));
	TestTrue(TEXT("finishes in the finish"), T.Update(0.5f, false, true, true, 5));
	TestFalse(TEXT("stopped"), T.Running.IsSet());
	TestEqual(TEXT("last time"), T.Last.GetValue(), 2.0f);
	TestEqual(TEXT("best time"), T.Best.GetValue(), 2.0f);
	// A slower run keeps the best; a faster one replaces it.
	T.Update(0.1f, false, true, false, 0);
	T.Update(2.9f, false, true, true, 5);
	TestEqual(TEXT("slower: last"), T.Last.GetValue(), 3.0f);
	TestEqual(TEXT("slower: best kept"), T.Best.GetValue(), 2.0f);
	T.Update(0.1f, false, true, false, 0);
	T.Update(1.4f, false, true, true, 5);
	TestEqual(TEXT("faster: new best"), T.Best.GetValue(), 1.5f);
	TestEqual(TEXT("formats as mm:ss.cc"), FRunlineTimer::Format(83.456f), FString(TEXT("01:23.45")));
	TestEqual(TEXT("never shows 60 seconds"), FRunlineTimer::Format(59.999f), FString(TEXT("00:59.99")));
	return true;
}

// Review Focus 4.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineTimerRespawn, "Runline.Timer.RespawnAtStartResets_LaterCheckpointKeeps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineTimerRespawn::RunTest(const FString&)
{
	FRunlineTimer T;
	T.Update(1.0f, false, true, false, 0);
	T.OnRespawn(2);
	TestTrue(TEXT("a respawn at a later checkpoint keeps the clock"), T.Running.IsSet());
	T.OnRespawn(0);
	TestFalse(TEXT("a respawn at the start resets it"), T.Running.IsSet());
	return true;
}

#endif
