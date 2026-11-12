/**
 * @file EditorAutomationStageReset.cpp
 * @brief 체인의 다음 단계 앞에서 편집기를 새 프로세스처럼 되돌립니다(`SW_AUTOMATION_STAGE_RESET`).
 * @details 세션은 씬 · 전역 변수 · 게임을 되돌리지만 편집기 상태는 모른다. 앞 단계가 연 팝업(Quick Open · 메뉴)은 다음 단계의 클릭을 삼키고,
 *          패널이 쥔 검색 칸 · 필터 · 뷰포트 토글과 닫힌 핵심 패널이 남아, 혼자 돌면 통과하는 시나리오가 체인에서 졌다(편집기 체인 2026-10-30, 27 개).
 *          새 프로세스와 같은 자리로: 재생 · 멀티 프로세스 재생을 멈추고 → 다음 Play 구성을 기본값으로 두고 → 팝업 · 입력 초점 · 탭 막대 선택 · 떠 있는 창의 기억을 풀고 → 편집기 상태 저장소를 처음 값으로 두고 → 패널을 모두 새로 만들고 → 기본 배치로 둔다.
 *          Play 구성은 `netplay.*` 명령이 바꾸면 남아 다음 단계의 툴바가 Net Mode 줄로 그려졌다(netsyncobjects 뒤 screen 의 씬 뷰가 1 화소 낮았다).
 */
#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorSaveFileLock.h"
#include "Editor/Common/GUI/Panel/EditorDockLayout.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Automation/Scenario/AutomationStageReset.h"

#include "imgui_internal.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAutomationStageResetInternal
        {
            /**
             * @brief 앞 단계가 누른 채 둔 키 · 단추를 놓습니다. 지우기(`ClearInputKeys`)가 아니라 뗌 이벤트다 - 입력을 게임 뷰로 넘기는 쪽(`ImGuiKeyForward`)도 뗌을 봐야 한다.
             * @details 단계가 `state="Down"` 뒤에 건너뜀 · 실패로 끝나면 놓는 단계가 돌지 않는다. 그대로 두면 다음 단계들의 클릭 · 단축키가 눌린 입력과 섞여 엉뚱한 단계가 진다.
             */
            static void releaseHeldInput()
            {
                ImGuiIO& io = ImGui::GetIO();
                for ( int32 key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_MouseLeft; ++key )
                {
                    if ( ImGui::GetKeyData( static_cast<ImGuiKey>( key ) )->Down )
                        io.AddKeyEvent( static_cast<ImGuiKey>( key ), false );
                }
                for ( const ImGuiKey modifier : { ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super } )
                {
                    if ( ImGui::GetKeyData( modifier )->Down )
                        io.AddKeyEvent( modifier, false );
                }
                for ( int32 button = 0; button < ImGuiMouseButton_COUNT; ++button )
                {
                    if ( io.MouseDown[button] )
                        io.AddMouseButtonEvent( button, false );
                }
            }

            static void resetEditor()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>(); // 선택: 편집기를 켜지 않은 체인(게임 시나리오)에는 없다
                if ( pContext == nullptr )
                    return;
                if ( EditorPlaySession::isPlaying() )
                    EditorPlaySession::stop();
                pContext->getSubsystem<EditorNetPlaySession>().resetToFreshEditor();
                if ( ImGui::GetCurrentContext() != nullptr )
                {
                    releaseHeldInput();
                    if ( ImGui::GetCurrentContext()->OpenPopupStack.Size > 0 )
                        ImGui::ClosePopupToLevel( 0, false );
                    ImGui::ClearActiveID();
                    // 탭 막대에서 고른 탭은 ImGui 가 id 로 기억해 패널을 새로 만들어도 남는다 — 앞 단계(netconditions)가 연 Profiler 의 Timeline 탭이
                    // 다음 단계의 프레임 그래프를 가렸다. 새 프로세스처럼 비운다(도킹 노드의 탭 막대는 노드가 가져 기본 배치가 다시 세운다).
                    ImGui::GetCurrentContext()->TabBars.Clear();
                    // 떠 있는 창(도구 창)은 ImGui 가 위치 · 크기를 기억해 `FirstUseEver` 가 다시 오지 않는다 - 앞 단계가 큰 창(1920x1080)에서 연 Render Targets 가
                    // 시작 크기로 돌아온 주 창 밖에 남았다. 새 프로세스처럼 잊게 한다(도킹된 창은 아래 기본 배치가 다시 세운다).
                    constexpr ImGuiWindowFlags kNotTopLevel = ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoSavedSettings;
                    for ( const ImGuiWindow* pWindow : ImGui::GetCurrentContext()->Windows )
                    {
                        if ( pWindow->DockId == 0 && ( pWindow->Flags & kNotTopLevel ) == 0 )
                            ImGui::ClearWindowSettings( pWindow->Name );
                    }
                }
                // 문서 포커스도 새 프로세스처럼 비운다 — 앞 단계(abilitycatalog)가 포커스한 abilities.xml 을 다음 단계가 연 카탈로그 패널이 받아 열었다(문서 패널은 열 때 지금 포커스를 받는다).
                pContext->getSubsystem<EditorWorkspace>().setFocusedAssetPath( "" );
                pContext->getSubsystem<EditorStateStore>().resetAll(); // 패널 밖 그리기 상태(테마 대화상자 · 툴바 · 메뉴) — 열린 테마 대화상자가 다음 단계의 클릭을 삼켰다. 앞 단계의 파일 잠금(`EditorSaveFileLock`)도 여기서 풀린다
                EditorSaveFileLock::restoreLeftovers();                // 죽은 앞 프로세스가 장부에 남긴 잠금 — 상태 소멸자가 돌지 못한 몫이다
                (void)pContext->getSubsystem<EditorPanelManager>().resetAllPanels();
                EditorDockLayout* pDock = pContext->findSubsystem<EditorDockLayout>(); // 선택: 셸이 도킹 배치를 알리기 전과 셸 없는 테스트 호스트에는 없다
                if ( pDock != nullptr )
                    pDock->requestResetDefault();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw
{
    SW_AUTOMATION_STAGE_RESET( editorSession, "editor (play, net play, popups, panels, layout)", &editor::EditorAutomationStageResetInternal::resetEditor );
} // namespace sw
