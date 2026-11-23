/**
 * @file EditorReflectionProbeBakeStatus.cpp
 * @brief 상태줄 항목 — 반사 프로브를 베이크하는 동안 "Baking reflection probes 1/3" 과 Cancel 단추를 보입니다(`EditorReflectionProbeBakeSession`).
 * @details 베이크하지 않을 때는 아무것도 그리지 않는다. 세션은 명령 층(3)이고 그리기는 GUI 층(4)이라 항목을 여기 둔다(`EditorLightProbeBakeStatus.cpp` 와 같은 자리).
 */
#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Status/EditorStatusBar.h"
#include "Editor/Common/GUI/Status/Model/EditorStatusViewModel.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        void drawReflectionProbeBakeStatus()
        {
            EditorProbeBakeStatus status;
            if ( EditorStatusViewModel::gatherReflectionProbeBake( status ) == false )
                return;
            fixed_string<constant::buffer::kMax128> label;
            formatstring( label.data(), label.capacity(), "Baking reflection probes %#/%#", status._done, status._total );
            ImGui::TextUnformatted( label.c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Cancel" ), "Cancel##reflectionProbeBake" ) ) )
                EditorStatusViewModel::cancelReflectionProbeBake();
        }
    } // namespace

    SW_EDITOR_STATUS_BAR_ITEM( ReflectionProbeBake, "reflection_probe_bake", 91, &drawReflectionProbeBakeStatus );
} // namespace sw::editor
