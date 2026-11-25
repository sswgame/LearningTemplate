#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Client/Automation/Tests/ScenarioFileCheck.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameSelection.h"

using namespace sw;

// 편집기 시나리오를 실행 없이 실행기의 읽기 · 단계 검사에 넣는다 — 편집기 단계는 편집기 모듈이 등록하므로 엔진의 ScenarioFilesTest 가 아니라 여기서 본다.
SW_TEST_CASE( EditorScenarioFilesTest, AllEditorScenariosParseResolveAndValidate )
{
    const IModuleLoader* const pModuleLoader = editor::findSubsystem<IModuleLoader>(); // 선택: 호스트 없는 하니스에는 없다 — 모듈을 요구하는 시나리오는 이름 검사를 건너뛴다
    SW_EXPECT_TRUE( test::validateScenarioFolder( "engine/automation/editor", pModuleLoader ) );
    SW_EXPECT_TRUE( test::validateScenarioFolder( "engine/automation/editor/statedir", pModuleLoader ) ); // 두 프로세스가 같은 상태 폴더를 이어 쓰는 쌍
    // 활성 게임의 편집기 시나리오(편집기 체인이 함께 돈다) — 없는 게임도 있다
    const string                            gameName = StringUtil::toLower( GameSelection::getName().c_str() );
    fixed_string<constant::buffer::kMax256> folder;
    formatstring( folder.data(), folder.capacity(), "game/%#/automation/editor", gameName );
    (void)test::validateScenarioFolder( folder, pModuleLoader );
    // 편집기를 켜고 혼자 도는 데스크톱 시나리오(플레이 중 리로드 — IntegrationTests/Scenario 가 따로 등록한다)
    formatstring( folder.data(), folder.capacity(), "game/%#/automation/desktop/editor", gameName );
    (void)test::validateScenarioFolder( folder, pModuleLoader );
}
