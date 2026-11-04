#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPanel.h"

#include "Core/Container/formatString.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Scene/EditorSceneViewUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPreview.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutPanelInternal
        {
            /** @brief 읽힌 공원 배치의 놀이기구 수입니다(감시 값 `Editor.ParkLayoutRideCount`). 배치를 읽지 않았으면 값을 내지 않는다. */
            [[nodiscard]] static bool readRideCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ParkLayoutPreview* pPreview = findSubsystem<ParkLayoutPreview>();
                if ( pPreview == nullptr || pPreview->isLoaded() == false )
                    return false;
                outValue = static_cast<float64>( pPreview->getRides().size() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorParkLayoutRideCount, "Editor.ParkLayoutRideCount", "Rides of the park layout the Park Layout preview read (fails while it has not read one)",
                         &ParkLayoutPanelInternal::readRideCount );

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
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITORICON( "Refresh" ), SW_LOCKEY( "EdPluginButton", "Reload" ), "Reload" ) ) )
            bForce = true;
        EditorSelfTestMarks::note( "themepark.layout.reload" );
        // [/doc:panel-draw]
        if ( preview.refresh( bForce ) && bForce )
            SW_LOG_INFO_ID( "ParkLayoutPanel.ParkLayoutReloadedRequest", "Park layout reloaded on request" );
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
        const vector<ParkRidePreview>& listRide = preview.getRides();
        for ( size_t rideIndex = 0; rideIndex < listRide.size(); ++rideIndex )
        {
            const ParkRidePreview& ride = listRide[rideIndex];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::PushID( static_cast<int32>( rideIndex ) );
            ImGui::Selectable( ride._name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
            if ( EditorInputGUI::wasItemDoubleClicked() )
                (void)EditorSceneViewUtil::focusOn( ride._position, MathUtil::max( ride._size._x, ride._size._z ) ); // 씬 뷰가 닫혀 있으면 할 일이 없다
            ImGui::PopID();
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "themepark.layout.ride.%#", rideIndex );
            EditorSelfTestMarks::note( mark.c_str() ); // 시나리오가 줄을 두 번 눌러 카메라를 옮긴다
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
    SW_EDITOR_PANEL( ParkLayoutPanel, ParkLayoutPanel::kPanelID, EditorPanelCategory::Custom, 9000 );
    // [/doc:panel-register]
} // namespace sw::editor
