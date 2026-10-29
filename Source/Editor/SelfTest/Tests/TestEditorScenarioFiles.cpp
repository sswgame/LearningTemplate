#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Tests/ScenarioFileCheck.h"

using namespace sw;

// 편집기 시나리오를 실행 없이 실행기의 읽기 · 단계 검사에 넣는다 — 편집기 단계는 편집기 모듈이 등록하므로 엔진의 ScenarioFilesTest 가 아니라 여기서 본다.
SW_TEST_CASE( EditorScenarioFilesTest, AllEditorScenariosParseResolveAndValidate )
{
    SW_EXPECT_TRUE( test::checkScenarioFolder( "engine/automation/editor" ) );
    SW_EXPECT_TRUE( test::checkScenarioFolder( "engine/automation/editor/chain" ) );    // 체인 사이에 끼우는 리로드
    SW_EXPECT_TRUE( test::checkScenarioFolder( "engine/automation/editor/statedir" ) ); // 두 프로세스가 같은 상태 폴더를 이어 쓰는 쌍
}
