#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/Tests/ScenarioFileCheck.h"
#include "Engine/Config/GameSelection.h"

using namespace sw;

// 편집기 시나리오를 실행 없이 실행기의 읽기 · 단계 검사에 넣는다 — 편집기 단계는 편집기 모듈이 등록하므로 엔진의 ScenarioFilesTest 가 아니라 여기서 본다.
SW_TEST_CASE( EditorScenarioFilesTest, AllEditorScenariosParseResolveAndValidate )
{
    const IModuleLoader* const pModuleLoader = editor::findSubsystem<IModuleLoader>(); // 선택: 호스트 없는 하니스에는 없다 — 모듈을 요구하는 시나리오는 이름 검사를 건너뛴다
    SW_EXPECT_TRUE( test::checkScenarioFolder( "engine/automation/editor", pModuleLoader ) );
    SW_EXPECT_TRUE( test::checkScenarioFolder( "engine/automation/editor/statedir", pModuleLoader ) ); // 두 프로세스가 같은 상태 폴더를 이어 쓰는 쌍
    // 활성 게임의 편집기 시나리오(편집기 체인이 함께 돈다) — 없는 게임도 있다
    (void)test::checkScenarioFolder( "game/" + StringUtil::toLower( GameSelection::getName().c_str() ) + "/automation/editor", pModuleLoader );
}
