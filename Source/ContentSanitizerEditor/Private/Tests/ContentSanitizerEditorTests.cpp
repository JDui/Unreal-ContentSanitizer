#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/SanitizerTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerPresentationTest, "ContentSanitizer.Unit.Presentation.SafeStatusText", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerPresentationTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Safe status has accessible text"), SanitizerClassificationToText(ESanitizerClassification::SafeDuplicate), FString(TEXT("Safe Duplicate")));
    return true;
}
#endif
