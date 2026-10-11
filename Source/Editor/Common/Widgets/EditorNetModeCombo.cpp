#include "pch.h"

#include "Editor/Common/Widgets/EditorNetModeCombo.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorNetPlaySession.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>

namespace sw::editor
{
    bool EditorNetModeCombo::draw( EditorNetPlaySession& session )
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
        EditorSelfTestMarks::note( "toolbar.netMode" );
        EditorWidgets::drawTooltip( "Net Mode — Standalone 은 에디터 안에서 재생하고, 나머지는 서버와 클라이언트를 따로 띄운다 (Window > Multiplayer)" );
        if ( settings._mode != EditorNetMode::Standalone )
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 4.0f );
            if ( ImGui::InputInt( "##PlayerCount", &settings._clientCount, 1, 1 ) )
                bChanged = true;
            settings._clientCount = MathUtil::clamp( settings._clientCount, 1, kMaxPlayerCount );
            EditorWidgets::drawTooltip( "플레이어 수 — 전용 서버면 클라이언트 수, 리슨 호스트면 호스트를 넣은 수" );
        }
        ImGui::EndDisabled();
        return bChanged;
    }
} // namespace sw::editor
