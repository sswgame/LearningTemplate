#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Tests/ScenarioFileCheck.h"
#include "Engine/Config/GameSelection.h"

using namespace sw;

// 실행 없이 모든 시나리오를 실행기의 읽기 · 단계 검사(`AutomationRunner::prepare` 앞부분)에 넣는다 — 엔진과 활성 게임의 자동화 폴더(하위 폴더 포함).
SW_TEST_CASE( ScenarioFilesTest, AllScenariosParseResolveAndValidate )
{
    const string gameFolder = "game/" + StringUtil::toLower( GameSelection::getName().c_str() ) + "/automation";
    SW_EXPECT_TRUE( test::checkScenarioFolder( "engine/automation" ) );
    (void)test::checkScenarioFolder( "engine/automation/rhiswap" ); // 조건이 붙은 시나리오(네 백엔드) — 없어도 된다
    (void)test::checkScenarioFolder( gameFolder );                  // 시나리오가 없는 게임도 있다
}
