/**
 * @file EditorAutomationStageReset.cpp
 * @brief 체인의 다음 단계 앞에서 편집기를 새 프로세스처럼 되돌립니다(`SW_AUTOMATION_STAGE_RESET`).
 * @details 세션은 씬 · 전역 변수 · 게임을 되돌리지만 편집기 상태는 모른다. 앞 단계가 연 팝업(Quick Open · 메뉴)은 다음 단계의 클릭을 삼키고,
 *          패널이 쥔 검색 칸 · 필터 · 뷰포트 토글과 닫힌 핵심 패널이 남아, 혼자 돌면 통과하는 시나리오가 체인에서 졌다(편집기 체인 2026-10-30, 27 개).
 *          새 프로세스와 같은 자리로: 재생을 멈추고 → 팝업 · 입력 초점을 풀고 → 패널을 모두 새로 만들고 → 기본 배치로 둔다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorDockLayout.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Automation/Runner/AutomationStageReset.h"

#include "imgui_internal.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAutomationStageResetInternal
        {
            static void resetEditor()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>(); // 선택: 편집기를 켜지 않은 체인(게임 시나리오)에는 없다
                if ( pContext == nullptr )
                    return;
                if ( EditorPlaySession::isPlaying() )
                    EditorPlaySession::stop();
                if ( ImGui::GetCurrentContext() != nullptr )
                {
                    if ( ImGui::GetCurrentContext()->OpenPopupStack.Size > 0 )
                        ImGui::ClosePopupToLevel( 0, false );
                    ImGui::ClearActiveID();
                }
                (void)pContext->getSubsystem<EditorPanelManager>().resetAllPanels( pContext->getSubsystem<EditorRenderViews>().getRHIDevice() );
                EditorDockLayout* pDock = pContext->findSubsystem<EditorDockLayout>(); // 선택: 셸이 도킹 배치를 알리기 전과 셸 없는 테스트 호스트에는 없다
                if ( pDock != nullptr )
                    pDock->requestResetDefault();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw
{
    SW_AUTOMATION_STAGE_RESET( editorSession, "editor (play, popups, panels, layout)", &editor::EditorAutomationStageResetInternal::resetEditor );
} // namespace sw
