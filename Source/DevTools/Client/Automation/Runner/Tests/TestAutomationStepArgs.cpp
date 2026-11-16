#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationStepArgs.h"

using namespace sw;

/** @brief [AutomationStepArgsTest] 열거형 변수는 열거자 이름으로 견준다 — 명령줄이 이름만 받으므로 `<Launch>` 의 이름 값이 "다르다" 로 읽히면 기동을 멈췄다(Scenario.shooter3d.desktop.devicelost) */
SW_TEST_CASE( AutomationStepArgsTest, EnumVariableComparesByEnumeratorName )
{
    GlobalVariableManager manager;
    int32                 value{ 1 }; // First
    SW_REQUIRE_TRUE( manager.registerVariable( "gv_testStepArgsEnum", GlobalVariableType::Enum, &value, int32{ 1 }, "", "TestMode" ) );
    manager.setEnumTextParser( SW_DELEGATE_LAMBDA( GlobalVariableEnumTextParser, []( string_view enumType, string_view text, int32& outValue ) -> bool
    {
        if ( enumType != "TestMode" )
            return false;
        if ( text == "First" )
            outValue = 1;
        else if ( text == "Second" )
            outValue = 2;
        else
            return false;
        return true;
    } ) );
    const GlobalVariableInfo* pInfo = manager.findVariable( "gv_testStepArgsEnum" );
    SW_REQUIRE_TRUE( pInfo != nullptr );

    bool bMatched = false;
    SW_EXPECT_TRUE( AutomationStepArgsUtil::compareVariable( *pInfo, "First", 0.0f, bMatched ) );
    SW_EXPECT_TRUE( bMatched );
    SW_EXPECT_TRUE( AutomationStepArgsUtil::compareVariable( *pInfo, "Second", 0.0f, bMatched ) );
    SW_EXPECT_FALSE( bMatched );
    SW_EXPECT_TRUE( AutomationStepArgsUtil::compareVariable( *pInfo, "1", 0.0f, bMatched ) );
    SW_EXPECT_TRUE( bMatched );
    SW_EXPECT_FALSE( AutomationStepArgsUtil::compareVariable( *pInfo, "Bogus", 0.0f, bMatched ) );
}
