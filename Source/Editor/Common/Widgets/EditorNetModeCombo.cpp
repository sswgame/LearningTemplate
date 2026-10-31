#include "pch.h"

#include "Editor/Common/Widgets/EditorNetModeCombo.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    bool EditorNetModeCombo::draw( EditorNetPlaySession& session, const utf8* pMark )
    {
        EditorNetPlaySettings& settings = session.getSettings();
        bool                   bChanged = false;
        ImGui::BeginDisabled( session.isRunning() );
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 9.0f );
        if ( ImGui::BeginCombo( "##NetMode", toString( settings._mode ) ) )
        {
            constexpr EditorNetMode kArrMode[] = { EditorNetMode::Standalone, EditorNetMode::DedicatedServer, EditorNetMode::ListenHost };
            for ( const EditorNetMode mode : kArrMode )
            {
                if ( ImGui::Selectable( toString( mode ), settings._mode == mode ) )
                {
                    settings._mode = mode;
                    bChanged       = true;
                }
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::note( pMark );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetMode" ) );
        if ( settings._mode != EditorNetMode::Standalone )
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 4.0f );
            if ( ImGui::InputInt( "##PlayerCount", &settings._clientCount, 1, 1 ) )
                bChanged = true;
            settings._clientCount = MathUtil::clamp( settings._clientCount, 1, kMaxPlayerCount );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetPlayerCount" ) );
        }
        ImGui::EndDisabled();
        return bChanged;
    }
} // namespace sw::editor
