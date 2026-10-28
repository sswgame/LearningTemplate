#include "pch.h"

#include "Editor/Common/GUI/EditorRedrawUtil.h"

#include "Editor/Common/Commands/Scene/EditorLightProbeBakeSession.h"
#include "Editor/Common/Commands/Scene/EditorReflectionProbeBakeSession.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "ModuleAPI/ABI/EditorAPI.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    uint32 EditorRedrawUtil::collectReasons()
    {
        EditorContext* const      pContext = editor::findSubsystem<EditorContext>();
        const ImGuiContext* const pImGui   = ImGui::GetCurrentContext();
        if ( pContext == nullptr || pImGui == nullptr )
            return static_cast<uint32>( EditorRedrawReason::None ); // 그릴 에디터 UI 가 없다
        // 이번 프레임의 NewFrame 이 처리한 입력(마우스 위치 · 단추 · 키 · 글자 · 포커스)과 누르고 있는 단추.
        const bool bInput = pImGui->InputEventsTrail.Size > 0 || ImGui::IsAnyMouseDown();
        // 잡고 있는 것 — 다음 프레임 입력이 없어도 화면이 따라가야 한다(끌기 미리보기 · 창 옮기기 · Ctrl+Tab 창 고르기).
        const bool bActive  = ImGui::IsAnyItemActive() || pImGui->MovingWindow != nullptr || pImGui->DragDropActive || pImGui->NavWindowingTarget != nullptr;
        const bool bRunning = ( EditorPlaySession::isPlayerActive() || EditorPlaySession::isSimulating() ) && EditorPlaySession::isPaused() == false;
        const bool bToast   = pContext->getSubsystem<EditorNotificationManager>().getNotifications().empty() == false;
        const bool bAnimate = pContext->getSubsystem<EditorRenderViews>().isAnimationRequested();
        // 프레임 단계가 나르는 일 — 워커가 끝내도 창 메시지가 오지 않으므로 끝날 때까지 깨어 있는다.
        const EditorLightProbeBakeSession* const      pLightBake        = editor::findSubsystem<EditorLightProbeBakeSession>();
        const EditorReflectionProbeBakeSession* const pReflectionBake   = editor::findSubsystem<EditorReflectionProbeBakeSession>();
        const bool                                    bLightBaking      = pLightBake != nullptr && pLightBake->isRunning();
        const bool                                    bReflectionBaking = pReflectionBake != nullptr && pReflectionBake->isRunning();
        const bool                                    bWorking          = pContext->getSubsystem<AssetHotReload>().isImportBusy() || bLightBaking || bReflectionBaking;
        uint32                                        reasons           = 0;
        reasons |= bInput ? static_cast<uint32>( EditorRedrawReason::Input ) : 0u;
        reasons |= bActive ? static_cast<uint32>( EditorRedrawReason::ActiveItem ) : 0u;
        reasons |= bRunning ? static_cast<uint32>( EditorRedrawReason::Simulation ) : 0u;
        reasons |= bToast ? static_cast<uint32>( EditorRedrawReason::Notification ) : 0u;
        reasons |= bAnimate ? static_cast<uint32>( EditorRedrawReason::Animation ) : 0u;
        reasons |= bWorking ? static_cast<uint32>( EditorRedrawReason::BackgroundWork ) : 0u;
        return reasons;
    }
} // namespace sw::editor
