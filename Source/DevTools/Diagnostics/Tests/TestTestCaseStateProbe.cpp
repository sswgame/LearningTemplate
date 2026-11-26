// TestCaseStateProbeTest — 단위 케이스가 흘린 프로세스 상태(되돌리지 않은 전역 변수 · 남은 태스크)를 이름으로 내고, 흘린 것이 다음 케이스의 기준이 되는지(nogpu).
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Diagnostics/TestCaseStateProbe.h"

#include "Engine/Foundation/EngineSubsystems.h"

using namespace sw;

/** @brief [TestCaseStateProbeTest] 되돌리지 않은 전역 변수는 그 이름으로 한 번 나오고, 되돌린 것 · 이미 알린 것은 나오지 않는다 */
SW_TEST_CASE( TestCaseStateProbeTest, UnrestoredVariableIsNamedOnce )
{
    GlobalVariableInfo* pInfo = engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_standardInputCommands" );
    SW_REQUIRE_NOT_NULL( pInfo );
    const int32        kept = pInfo->getValueAsInt();
    TestCaseStateProbe probe;
    vector<string>     listLeak;
    probe.beginCase();
    probe.endCase( listLeak );
    SW_EXPECT_TRUE( listLeak.empty() );

    probe.beginCase();
    pInfo->setValueAsInt( kept + 1 );
    pInfo->setValueAsInt( kept );
    probe.endCase( listLeak );
    SW_EXPECT_TRUE( listLeak.empty() ); // 되돌렸다

    probe.beginCase();
    pInfo->setValueAsInt( kept + 1 );
    probe.endCase( listLeak );
    SW_EXPECT_EQUAL( size_t{ 1 }, listLeak.size() );
    SW_EXPECT_TRUE( listLeak.empty() == false && StringUtil::contains( listLeak[0], "gv_standardInputCommands" ) );

    listLeak.clear();
    probe.beginCase();
    probe.endCase( listLeak );
    SW_EXPECT_TRUE( listLeak.empty() ); // 흘린 값이 기준이 됐다
    pInfo->setValueAsInt( kept );
}
