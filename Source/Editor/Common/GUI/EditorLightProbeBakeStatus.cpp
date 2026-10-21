/**
 * @file EditorLightProbeBakeStatus.cpp
 * @brief 상태줄 항목 — 라이트 프로브를 굽는 동안 "Baking probes 2/3 (41 / 100)" 와 Cancel 단추를 보입니다(`EditorLightProbeBakeSession`).
 * @details 굽지 않을 때는 아무것도 그리지 않는다. 세션은 명령 층(3)이고 그리기는 GUI 층(4)이라 항목을 여기 둔다.
 */
#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Scene/EditorLightProbeBakeSession.h"
#include "Editor/Common/GUI/EditorStatusBar.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        void drawLightProbeBakeStatus()
        {
            EditorLightProbeBakeSession* pSession = editor::findSubsystem<EditorLightProbeBakeSession>();
            if ( pSession == nullptr || pSession->isRunning() == false )
                return;
            fixed_string<constant::kMaxBuffer128> label;
            formatstring( label.data(), label.capacity(), "Baking probes %#/%# (%# / 100)", pSession->getVolumeIndex() + 1, pSession->getVolumeCount(),
                          static_cast<uint32>( pSession->getVolumeProgress() * 100.0f ) );
            ImGui::TextUnformatted( label.c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Cancel##lightProbeBake" ) )
                pSession->cancel( "canceled" );
            EditorSelfTestMarks::note( "statusBar.lightProbeBakeCancel" ); // 시나리오 lightprobebakecancel 이 누른다
        }
    } // namespace

    SW_EDITOR_STATUS_BAR_ITEM( LightProbeBake, "light_probe_bake", 90, &drawLightProbeBakeStatus );
} // namespace sw::editor
