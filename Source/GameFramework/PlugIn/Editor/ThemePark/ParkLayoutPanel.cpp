#include "pch.h"

#include "GameFramework/PlugIn/Editor/ThemePark/ParkLayoutPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Scene/EditorSceneViewUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "GameFramework/PlugIn/Editor/ThemePark/ParkLayoutPreview.h"

#include <imgui.h>

namespace sw::editor
{
    // [doc:panel-draw]
    ParkLayoutPanel::ParkLayoutPanel()
        : IEditorPanel( false )
    {
    }

    void ParkLayoutPanel::drawContent()
    {
        ParkLayoutPreview* pPreview = findSubsystem<ParkLayoutPreview>();
        if ( pPreview == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        ParkLayoutPreview& preview = *pPreview;
        bool               bForce  = false;
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Reload" ) ) )
            bForce = true;
        EditorSelfTestMarks::note( "themepark.layout.reload" );
        // [/doc:panel-draw]
        if ( preview.refresh( bForce ) && bForce )
            SW_LOG_INFO( "Park layout reloaded on request" );
        if ( preview.isLoaded() == false )
        {
            const string hint = ( preview.isMissing() ? "This game has no park layout data (" : "The park layout could not be read (" ) + preview.getLayoutPath() + ")";
            EditorWidgets::drawEmptyHint( hint.c_str() );
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

    // [doc:panel-register]
    SW_EDITOR_PANEL( ParkLayoutPanel, "themepark.layout", EditorPanelCategory::Custom, 9000 );
    // [/doc:panel-register]
} // namespace sw::editor
