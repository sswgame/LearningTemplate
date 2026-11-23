/**
 * @file EditorLightProbeBakeStatus.cpp
 * @brief 상태줄 항목 — 라이트 프로브를 베이크하는 동안 "Baking probes 2/3 (41 / 100)" 와 Cancel 단추를 보입니다(`EditorLightProbeBakeSession`).
 * @details 베이크하지 않을 때는 아무것도 그리지 않는다. 세션은 명령 층(3)이고 그리기는 GUI 층(4)이라 항목을 여기 둔다.
 */
#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Status/EditorStatusBar.h"
#include "Editor/Common/GUI/Status/Model/EditorStatusViewModel.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        void drawLightProbeBakeStatus()
        {
            EditorProbeBakeStatus status;
            if ( EditorStatusViewModel::gatherLightProbeBake( status ) == false )
                return;
            fixed_string<constant::buffer::kMax128> label;
            formatstring( label.data(), label.capacity(), "Baking probes %#/%# (%# / 100)", status._done, status._total, static_cast<uint32>( status._itemProgress * 100.0f ) );
            ImGui::TextUnformatted( label.c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Cancel" ), "Cancel##lightProbeBake" ) ) )
                EditorStatusViewModel::cancelLightProbeBake();
            EditorSelfTestMarks::record( "statusBar.lightProbeBakeCancel" ); // 시나리오 lightprobebakecancel 이 누른다
        }
    } // namespace

    SW_EDITOR_STATUS_BAR_ITEM( LightProbeBake, "light_probe_bake", 90, &drawLightProbeBakeStatus );
} // namespace sw::editor
