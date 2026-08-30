#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Operations/SanitizerActionPlan.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerClassificationContractTest, "ContentSanitizer.Unit.Classification.SamePayloadDifferentSettingsIsReview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerClassificationContractTest::RunTest(const FString& Parameters)
{
    FSanitizerDuplicateGroup Group;
    Group.Classification = ESanitizerClassification::ReviewRequired;
    Group.CanonicalMemberIndex = 0;
    Group.Members.SetNum(2);
    FSanitizerActionPlan Plan;
    FString Error;
    TestFalse(TEXT("ReviewRequired groups are never action-plan eligible"), FSanitizerActionPlanner::CreatePlan(Group, 1, Plan, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerPreflightShapeTest, "ContentSanitizer.Unit.Preflight.InvalidPlanIsBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerPreflightShapeTest::RunTest(const FString& Parameters)
{
    FSanitizerActionPlan Plan;
    const FSanitizerPreflightResult Result = FSanitizerActionPlanner::ValidateShape(Plan);
    TestEqual(TEXT("Invalid empty plan is blocked"), Result.Status, ESanitizerPreflightStatus::Blocked);
    return true;
}
#endif
