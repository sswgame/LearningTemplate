#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationStepArgs.h"

using namespace sw;

/** @brief [AutomationStepArgsTest] 열거형 변수는 열거자 이름으로 견준다 — 명령줄이 이름만 받으므로 `<Launch>` 의 이름 값이 "다르다" 로 읽히면 기동을 멈췄다(Scenario.playground.desktop.devicelost) */
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

/** @brief [AutomationStepArgsTest] 수를 글로 적을 때 지수의 0 을 지우지 않는다 — `<Record>` · `is` 비교 · 보고가 읽는 글이다 */
SW_TEST_CASE( AutomationStepArgsTest, NumbersKeepTheirExponent )
{
    SW_EXPECT_EQUAL( string( "1" ), AutomationStepArgsUtil::formatNumber( 1.0 ) );
    SW_EXPECT_EQUAL( string( "10" ), AutomationStepArgsUtil::formatNumber( 10.0 ) );
    SW_EXPECT_EQUAL( string( "0.5" ), AutomationStepArgsUtil::formatNumber( 0.5 ) );
    SW_EXPECT_EQUAL( string( "1.5e-10" ), AutomationStepArgsUtil::formatNumber( 1.5e-10 ) );
    SW_EXPECT_EQUAL( string( "1.5e+20" ), AutomationStepArgsUtil::formatNumber( 1.5e20 ) );
}
