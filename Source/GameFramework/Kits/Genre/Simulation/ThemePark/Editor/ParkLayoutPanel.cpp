#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorSceneViewUtil.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPreview.h"

#include <imgui.h>

namespace sw::editor
{
    ParkLayoutPanel::ParkLayoutPanel()
        : IEditorPanel( false )
    {
    }

    void ParkLayoutPanel::drawContent()
    {
        ParkLayoutPreview& preview = ParkLayoutPreview::get();
        bool               bForce  = false;
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Reload" ) ) )
            bForce = true;
        EditorSelfTestMarks::note( "themepark.layout.reload" );
        if ( preview.refresh( bForce ) && bForce )
            SW_LOG_INFO( "Park layout reloaded on request" );
        if ( preview.isLoaded() == false )
        {
            EditorWidgets::drawEmptyHint( "game/themepark/data/rides.xml could not be read" );
            return;
        }

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
        if ( ImGui::BeginTable( "##ParkRides", 4, kTableFlags ) == false )
            return;
        ImGui::TableSetupColumn( "Ride" );
        ImGui::TableSetupColumn( "Cost" );
        ImGui::TableSetupColumn( "Capacity" );
        ImGui::TableSetupColumn( "Load (s)" );
        ImGui::TableHeadersRow();
        for ( const ParkRidePreview& ride : preview.getRides() )
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::Selectable( ride._name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
            if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                (void)EditorSceneViewUtil::focusOn( ride._position, MathUtil::max( ride._size._x, ride._size._z ) ); // 씬 뷰가 닫혀 있으면 할 일이 없다
            ImGui::TableSetColumnIndex( 1 );
            ImGui::Text( "%d", ride._buildCost );
            ImGui::TableSetColumnIndex( 2 );
            ImGui::Text( "%d", ride._capacity );
            ImGui::TableSetColumnIndex( 3 );
            ImGui::Text( "%.0f", static_cast<float64>( ride._loadTime ) );
        }
        ImGui::EndTable();
    }

    SW_EDITOR_PANEL( ParkLayoutPanel, "themepark.layout", EditorPanelCategory::Custom, 9000 );
} // namespace sw::editor
