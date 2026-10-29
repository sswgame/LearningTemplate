#include "pch.h"

#include "GameFramework/Plugin/Editor/Base/Schedule/ScheduleDebugPanel.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ScheduleDebugPanelInternal
        {
            /** @brief 감시 값이 읽는 값 하나입니다 — 패널 인스턴스가 게시한 값 중 @p pField 를 @p outValue 에 씁니다. 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readPublished( uint32 ScheduleDebugPublished::* pField, float64& outValue )
            {
                const EditorPanelManager* pManager = editor::findSubsystem<EditorPanelManager>();
                const ScheduleDebugPanel* pPanel   = pManager != nullptr ? pManager->findPanel<ScheduleDebugPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPublished().*pField );
                return true;
            }

            [[nodiscard]] static bool readNPCCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &ScheduleDebugPublished::_npcCount, outValue );
            }
            [[nodiscard]] static bool readTimelineRowCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &ScheduleDebugPublished::_timelineRowCount, outValue );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ScheduleDebugPanel::ScheduleDebugPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _model{}
    {
    }

    void ScheduleDebugPanel::drawContent()
    {
        const ScheduleSystem* pSystem = _model.refreshSystems();
        if ( pSystem == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdSchedule", "EmptyHint" ) );
            return;
        }
        if ( _model.getSystemCount() > 1 )
        {
            int32 systemIndex = _model.getSystemIndex();
            EditorChrome::setNextItemWidthColumns( 16.0f );
            if ( ImGui::InputInt( SW_LOCKEY( "EdSchedule", "SystemIndex" ), &systemIndex ) )
                pSystem = _model.selectSystem( systemIndex );
        }
        const ScheduleSystem& system = *pSystem;

        if ( ImGui::BeginChild( "##ScheduleNPCList", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 27.0f, 0.0f ) ), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX ) )
            drawNPCList( system );
        ImGui::EndChild();
        ImGui::SameLine();

        if ( ImGui::BeginChild( "##ScheduleDetail" ) )
        {
            const int32 npcIndex = _model.findSelectedNPCIndex( system );
            if ( npcIndex < 0 )
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdSchedule", "SelectNPCHint" ) );
            }
            else
            {
                drawTimeline( system, npcIndex );
                drawExplanation( system, npcIndex );
            }
        }
        ImGui::EndChild();
    }

    void ScheduleDebugPanel::drawNPCList( const ScheduleSystem& system )
    {
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdSchedule", "NPCListTitle" ) );
        ImGui::Separator();
        for ( int32 npcIndex = 0; npcIndex < system.getNPCCount(); ++npcIndex )
        {
            const hashed_string npcID     = system.getNPCID( npcIndex );
            const bool          bSelected = npcID == _model.getSelectedNPC();
            ImGui::PushID( npcIndex );
            if ( ImGui::Selectable( npcID.c_str(), bSelected ) )
                _model.selectNPC( npcID );
            EditorSelfTestMarks::note( ( string( "schedule.npc." ) + npcID.c_str() ).c_str() ); // 자동화가 NPC 를 고를 자리
            ImGui::PopID();
        }
    }

    void ScheduleDebugPanel::drawTimeline( const ScheduleSystem& system, int32 npcIndex )
    {
        const int32 nowMinute = system.getMinute() % kScheduleMinutesPerDay;
        ImGui::Checkbox( SW_LOCKEY( "EdSchedule", "FollowClock" ), &_model.editFollowClock() );
        _model.followClock( system );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -1.0f );
        int32        previewMinute = _model.getPreviewMinute();
        const string clockText     = ScheduleTimelineRows::formatClock( previewMinute );
        if ( ImGui::SliderInt( "##SchedulePreviewMinute", &previewMinute, 0, kScheduleMinutesPerDay - 1, clockText.c_str() ) )
            _model.setPreviewMinute( previewMinute );
        ImGui::TextDisabled( "%s %s (day %d)", SW_LOCKEY( "EdSchedule", "GameTime" ), ScheduleTimelineRows::formatClock( nowMinute ).c_str(), system.getDay() );

        const vector<ScheduleTimelineRow>& listRow = _model.refreshTimeline( system, npcIndex );
        const ImGuiTableFlags              flags   = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if ( ImGui::BeginTable( "##ScheduleTimeline", 5, flags, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 14.0f ) ) ) )
        {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnTime" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnTravel" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnPlace" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnActivity" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnSource" ) );
            ImGui::TableHeadersRow();
            for ( const ScheduleTimelineRow& row : listRow )
            {
                ImGui::TableNextRow();
                if ( row._bActive )
                    ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32( ImGuiCol_Header ) );
                if ( row._bPast )
                    ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( row._time.c_str() );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( row._travel.c_str() );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::TextUnformatted( row._place.c_str() );
                ImGui::TableSetColumnIndex( 3 );
                ImGui::TextUnformatted( row._activity.c_str() );
                ImGui::TableSetColumnIndex( 4 );
                ImGui::TextUnformatted( row._source.c_str() );
                if ( row._bPast )
                    ImGui::PopStyleColor();
            }
            if ( listRow.empty() )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdSchedule", "EmptyPlan" ) );
            }
            ImGui::EndTable();
        }
    }

    void ScheduleDebugPanel::drawExplanation( const ScheduleSystem& system, int32 npcIndex )
    {
        // explainNPC · dumpTimeline 은 지금 시각 기준이다 — 슬라이더 미리 보기와 상관없이 그대로 보인다.
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSchedule", "WhyNow" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            const string& text = _model.explain( system, npcIndex );
            ImGui::TextUnformatted( text.c_str(), text.c_str() + text.size() );
        }
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSchedule", "RawTimeline" ) ) )
        {
            const string& text = _model.dumpTimeline( system, npcIndex );
            ImGui::TextUnformatted( text.c_str(), text.c_str() + text.size() );
        }
    }

    SW_EDITOR_PANEL( ScheduleDebugPanel, ScheduleDebugPanel::kPanelID, EditorPanelCategory::Custom, 9310 );

    SW_AUTOMATION_WATCH( editorScheduleNPCCount, "Editor.ScheduleNPCCount", "NPCs of the schedule system the NPC Schedule window shows",
                         &ScheduleDebugPanelInternal::readNPCCount );
    SW_AUTOMATION_WATCH( editorScheduleTimelineRowCount, "Editor.ScheduleTimelineRowCount", "Rows of today's timeline for the NPC picked in the NPC Schedule window",
                         &ScheduleDebugPanelInternal::readTimelineRowCount );
} // namespace sw::editor
