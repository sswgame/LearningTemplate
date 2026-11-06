#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPanel.h"

#include "Core/Container/formatString.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Scene/EditorSceneViewUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

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

            /** @brief 정문으로 카메라를 옮길 때 담을 반지름(m)입니다. 정문은 데이터에 크기가 없다. */
            static constexpr float32 kGateFocusRadius = 4.0f;

            /** @brief 표 칸의 오른쪽 끝에 글을 그립니다(표의 숫자는 오른쪽 정렬이다 — docs/agents/EditorUI.md). */
            static void drawRightAligned( const utf8* pText, bool bMuted )
            {
                const float32 textWidth = ImGui::CalcTextSize( pText ).x;
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + MathUtil::max( 0.0f, ImGui::GetContentRegionAvail().x - textWidth ) );
                if ( bMuted )
                    ImGui::TextDisabled( "%s", pText );
                else
                    ImGui::TextUnformatted( pText );
            }

            static void drawNumberCell( int32 value )
            {
                fixed_string<constant::buffer::kMax32> text;
                formatstring( text.data(), text.capacity(), "%#", value );
                drawRightAligned( text.c_str(), false );
            }

            /** @brief 놀이기구의 배치 문제를 한 줄로 만듭니다. */
            static string makeIssueText( const ParkLayoutModel& model, const ParkRidePreview& ride )
            {
                string text;
                for ( const ParkRideIssue& issue : ride._listIssue )
                {
                    if ( text.empty() == false )
                        text += ", ";
                    const string& otherName = model.getRides()[issue._otherRide]._name;
                    switch ( issue._kind )
                    {
                        case ParkRideIssueKind::Overlap:
                            text += SW_LOCKEYFORMAT( "EdPluginParkLayout", "IssueOverlap", TextArgumentList().addText( "name", otherName ) );
                            break;
                        case ParkRideIssueKind::EntranceInsideFootprint:
                            text += SW_LOCKEY( "EdPluginParkLayout", "IssueEntrance" );
                            break;
                        case ParkRideIssueKind::DuplicateID:
                            text += SW_LOCKEYFORMAT( "EdPluginParkLayout", "IssueDuplicateID", TextArgumentList().addText( "name", otherName ) );
                            break;
                    }
                }
                return text;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorParkLayoutRideCount, "Editor.ParkLayoutRideCount", "Rides of the park layout the Park Layout preview read (fails while it has not read one)",
                         &ParkLayoutPanelInternal::readRideCount );

    // [doc:panel-draw]
    ParkLayoutPanel::ParkLayoutPanel()
        : IEditorPanel( false )
        , _filter{}
    {
    }

    void ParkLayoutPanel::drawContent()
    {
        ParkLayoutPreview* pPreview = findSubsystem<ParkLayoutPreview>();
        if ( pPreview == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        ParkLayoutPreview& preview = *pPreview;
        bool               bForce  = false;
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdPluginButton", "Reload" ), "Reload" ) ) )
            bForce = true;
        EditorSelfTestMarks::note( "themepark.layout.reload" );
        // [/doc:panel-draw]
        const bool bReread = preview.refresh( bForce );
        if ( preview.isLoaded() == false )
        {
            const string hint = preview.isMissing() ? SW_LOCKEYFORMAT( "EdPluginParkLayout", "NoLayoutHint", TextArgumentList().addText( "path", preview.getLayoutPath() ) )
                                                    : SW_LOCKEYFORMAT( "EdPluginParkLayout", "ReadFailedHint", TextArgumentList().addText( "path", preview.getLayoutPath() ) );
            EditorWidgets::drawEmptyHint( hint.c_str() );
            return;
        }
        if ( bReread && bForce )
            SW_LOG_INFO_ID( "ParkLayoutPanel.ParkLayoutReloadedRequest", "Park layout reloaded on request" );

        ParkLayoutModel& model = preview.editModel();
        ImGui::SameLine();
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Map" ), SW_LOCKEY( "EdPluginParkLayout", "FocusGate" ), "FocusGate" ) ) )
            (void)EditorSceneViewUtil::focusOn( model.getGatePosition(), ParkLayoutPanelInternal::kGateFocusRadius ); // 씬 뷰가 닫혀 있으면 할 일이 없다
        EditorSelfTestMarks::note( "themepark.layout.gate" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginParkLayout", "FocusGateTip" ) );

        // 정렬은 표를 연 뒤에야 알 수 있어 건수는 거르기로 먼저 센다(정렬은 건수를 바꾸지 않는다).
        const uint32           rideCount = static_cast<uint32>( model.getRides().size() );
        const EditorListFilter filter{ _filter.c_str() };
        vector<uint32>         listRow;
        model.collectRows( filter, ParkRideSortColumn::FileOrder, false, listRow );
        (void)EditorWidgets::drawFilterBar( "##ParkRideFilter", _filter, nullptr, static_cast<uint32>( listRow.size() ), rideCount, nullptr, "themepark.layout.filter" );
        if ( listRow.empty() )
        {
            EditorWidgets::drawNoSearchResultHint( filter.getText() );
            return;
        }

        // 맨 아래 상태 줄 자리를 남긴다. 처음 정렬은 파일 순서다(SortTristate — 없으면 첫 칸으로 정렬된 채 시작한다).
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;
        if ( ImGui::BeginTable( "##ParkRides", 6, kTableFlags, ImVec2{ 0.0f, -ImGui::GetFrameHeightWithSpacing() } ) )
        {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdPluginParkLayout", "ColumnRide" ), ImGuiTableColumnFlags_None, 0.0f, static_cast<ImGuiID>( ParkRideSortColumn::Name ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdPluginParkLayout", "ColumnCost" ), ImGuiTableColumnFlags_None, 0.0f, static_cast<ImGuiID>( ParkRideSortColumn::BuildCost ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdPluginParkLayout", "ColumnCapacity" ), ImGuiTableColumnFlags_None, 0.0f, static_cast<ImGuiID>( ParkRideSortColumn::Capacity ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdPluginParkLayout", "ColumnTime" ), ImGuiTableColumnFlags_None, 0.0f, static_cast<ImGuiID>( ParkRideSortColumn::Time ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdPluginParkLayout", "ColumnPrice" ), ImGuiTableColumnFlags_None, 0.0f, static_cast<ImGuiID>( ParkRideSortColumn::Price ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdPluginParkLayout", "ColumnCheck" ), ImGuiTableColumnFlags_NoSort );
            ImGui::TableHeadersRow();
            const ImGuiTableSortSpecs* pSortSpecs = ImGui::TableGetSortSpecs();
            if ( pSortSpecs != nullptr && pSortSpecs->SpecsCount > 0 )
                model.collectRows( filter, static_cast<ParkRideSortColumn>( pSortSpecs->Specs[0].ColumnUserID ), pSortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending, listRow );

            const vector<ParkRidePreview>& listRide = model.getRides();
            for ( const uint32 rideIndex : listRow )
            {
                const ParkRidePreview& ride = listRide[rideIndex];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::PushID( static_cast<int32>( rideIndex ) );
                if ( ImGui::Selectable( ride._name.c_str(), rideIndex == model.getSelectedRide(), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) )
                    model.selectRide( rideIndex );
                if ( EditorInputGUI::wasItemDoubleClicked() )
                    (void)EditorSceneViewUtil::focusOn( ride.computeFocusCenter(), ride.computeFocusRadius() ); // 씬 뷰가 닫혀 있으면 할 일이 없다
                ImGui::PopID();
                fixed_string<constant::buffer::kMax64> mark;
                formatstring( mark.data(), mark.capacity(), "themepark.layout.ride.%#", rideIndex );
                EditorSelfTestMarks::note( mark.c_str() ); // 시나리오가 줄을 두 번 눌러 카메라를 옮긴다 — 번호는 파일 순서라 정렬해도 그대로다
                ImGui::TableSetColumnIndex( 1 );
                ParkLayoutPanelInternal::drawNumberCell( ride._buildCost );
                ImGui::TableSetColumnIndex( 2 );
                ParkLayoutPanelInternal::drawNumberCell( ride._capacity );
                ImGui::TableSetColumnIndex( 3 );
                ParkLayoutPanelInternal::drawNumberCell( static_cast<int32>( ride._timeSeconds ) );
                ImGui::TableSetColumnIndex( 4 );
                if ( ride._bCoaster )
                    ParkLayoutPanelInternal::drawRightAligned( "-", true ); // 코스터의 탑승료는 테스트 운행이 정한다
                else
                    ParkLayoutPanelInternal::drawNumberCell( ride._price );
                ImGui::TableSetColumnIndex( 5 );
                if ( ride._listIssue.empty() == false )
                    ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "%s", ParkLayoutPanelInternal::makeIssueText( model, ride ).c_str() );
            }
            ImGui::EndTable();
        }
        const string status = SW_LOCKEYFORMAT( "EdPluginParkLayout", "Summary",
                                               TextArgumentList()
                                                   .addInteger( "cost", model.computeTotalBuildCost() )
                                                   .addInteger( "cash", model.getStartingCash() )
                                                   .addInteger( "fee", model.getEntryFee() )
                                                   .addInteger( "issues", model.countIssues() ) );
        EditorWidgets::drawPanelStatus( status.c_str() );
    }

    // [doc:panel-register]
    SW_EDITOR_PANEL( ParkLayoutPanel, ParkLayoutPanel::kPanelID, EditorPanelCategory::Custom, 9000 );
    // [/doc:panel-register]
} // namespace sw::editor
