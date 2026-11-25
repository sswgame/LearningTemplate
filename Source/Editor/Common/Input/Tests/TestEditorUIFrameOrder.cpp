/**
 * @file TestEditorUIFrameOrder.cpp
 * @brief 한 호스트 프레임의 순서 계약을 봅니다. UI 가 읽는 Model 값(명령 켜짐 값 묶음)은 UI 프레임을 열기 직전(`EditorCommandGUI::syncWithRegistry`, 셸의 `prepareFrameState`)에 나므로 늦는 프레임이 없다.
 * @details 스레드를 나누지 않는다(사용자 결정): 준비 → UI 프레임 → 명령 적용 · 게임 단계 → 씬 틱이 한 스레드에서 돈다. 에디터 안에서 돈다(에디터 컨텍스트 필요).
 *          등록부를 다시 만드는 길(`EditorCommandGUI::registerDefaults` — 모듈 리로드 · 첫 프레임과 같은 길)을 밟고, 준비 한 번으로 그 프레임의 메뉴가 켜지는지 본다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Input/EditorCommandGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

/**
 * @brief [EditorUIFrameOrderTest] 등록부를 다시 만든 뒤 프레임 앞 준비 한 번이면 같은 프레임의 메뉴가 켜짐 값을 읽는다(다음 프레임을 기다리지 않는다)
 */
SW_TEST_CASE( EditorUIFrameOrderTest, MenusReadValuesPublishedBeforeTheUIFrame )
{
    sw::editor::EditorContext* pContext = sw::editor::findSubsystem<sw::editor::EditorContext>();
    SW_REQUIRE_NOT_NULL( pContext );
    sw::editor::EditorCommandRegistry& registry = pContext->getSubsystem<sw::editor::EditorCommandRegistry>();

    sw::editor::EditorCommandGUI::registerDefaults();                                      // 모듈 리로드 · 기동과 같은 다시 만들기
    const sw::editor::EditorCommandDesc* pDesc = registry.find( "editor.commandPalette" ); // 켜짐 조건 없는 전역 명령(기본 표)
    SW_REQUIRE_NOT_NULL( pDesc );
    const uint32 commandIndex = static_cast<uint32>( pDesc - registry.getCommands().data() );
    SW_EXPECT_FALSE( registry.isEnabledInSnapshot( commandIndex ) ); // 다시 만든 뒤 아직 내지 않았다 — 낡은 인덱스를 읽지 않는다

    sw::editor::EditorCommandGUI::syncWithRegistry(); // UI 프레임을 열기 직전(셸이 프레임마다 부른다)
    SW_EXPECT_TRUE( registry.isEnabledInSnapshot( commandIndex ) );
}
