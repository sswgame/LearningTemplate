#include "pch.h"

#include "GameFramework/PlugIn/Editor/Base/Schedule/ScheduleDebugPanel.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Localization/LocText.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"

#include <imgui.h>

namespace sw::editor
{
    ScheduleDebugPanel::ScheduleDebugPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _listRow{}
        , _text{}
        , _selectedNpc{}
        , _systemIndex{ 0 }
        , _previewMinute{ 0 }
        , _bFollowClock{ true }
    {
    }

    void ScheduleDebugPanel::drawContent()
    {
        vector<const ScheduleSystem*> listSystem;
        ScheduleSystem::collectLiveSystems( listSystem );
        vector<const ScheduleSystem*> listActive;
        for ( const ScheduleSystem* pSystem : listSystem )
        {
            if ( pSystem != nullptr && pSystem->getNpcCount() > 0 )
                listActive.push_back( pSystem );
        }
        if ( listActive.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdSchedule", "EmptyHint" ) );
            return;
        }
        if ( _systemIndex < 0 || _systemIndex >= static_cast<int32>( listActive.size() ) )
            _systemIndex = 0;
        if ( listActive.size() > 1 )
        {
            ImGui::SetNextItemWidth( 120.0f );
            ImGui::InputInt( SW_LOCKEY( "EdSchedule", "SystemIndex" ), &_systemIndex );
            if ( _systemIndex < 0 || _systemIndex >= static_cast<int32>( listActive.size() ) )
                _systemIndex = 0;
        }
        const ScheduleSystem& system = *listActive[static_cast<size_t>( _systemIndex )];

        if ( ImGui::BeginChild( "##ScheduleNpcList", ImVec2{ 200.0f, 0.0f }, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX ) )
            drawNpcList( system );
        ImGui::EndChild();
        ImGui::SameLine();

        if ( ImGui::BeginChild( "##ScheduleDetail" ) )
        {
            const int32 npcIndex = _selectedNpc.empty() ? -1 : system.findNpcIndex( _selectedNpc );
            if ( npcIndex < 0 )
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdSchedule", "SelectNpcHint" ) );
            }
            else
            {
                drawTimeline( system, npcIndex );
                drawExplanation( system, npcIndex );
            }
        }
        ImGui::EndChild();
    }

    void ScheduleDebugPanel::drawNpcList( const ScheduleSystem& system )
    {
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdSchedule", "NpcListTitle" ) );
        ImGui::Separator();
        for ( int32 npcIndex = 0; npcIndex < system.getNpcCount(); ++npcIndex )
        {
            const hashed_string npcID     = system.getNpcID( npcIndex );
            const bool          bSelected = npcID == _selectedNpc;
            ImGui::PushID( npcIndex );
            if ( ImGui::Selectable( npcID.c_str(), bSelected ) )
                _selectedNpc = npcID;
            ImGui::PopID();
        }
    }

    void ScheduleDebugPanel::drawTimeline( const ScheduleSystem& system, int32 npcIndex )
    {
        const int32 nowMinute = system.getMinute() % kScheduleMinutesPerDay;
        ImGui::Checkbox( SW_LOCKEY( "EdSchedule", "FollowClock" ), &_bFollowClock );
        if ( _bFollowClock )
            _previewMinute = nowMinute;
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -1.0f );
        const string clockText = ScheduleTimelineRows::formatClock( _previewMinute );
        if ( ImGui::SliderInt( "##SchedulePreviewMinute", &_previewMinute, 0, kScheduleMinutesPerDay - 1, clockText.c_str() ) )
            _bFollowClock = false;
        ImGui::TextDisabled( "%s %s (day %d)", SW_LOCKEY( "EdSchedule", "GameTime" ), ScheduleTimelineRows::formatClock( nowMinute ).c_str(), system.getDay() );

        ScheduleTimelineRows::populate( system.getPlan( npcIndex ), _previewMinute, _listRow );
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if ( ImGui::BeginTable( "##ScheduleTimeline", 5, flags, ImVec2{ 0.0f, 260.0f } ) )
        {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnTime" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnTravel" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnPlace" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnActivity" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EdSchedule", "ColumnSource" ) );
            ImGui::TableHeadersRow();
            for ( const ScheduleTimelineRow& row : _listRow )
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
            if ( _listRow.empty() )
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
        // explainNpc · dumpTimeline 은 지금 시각 기준이다 — 슬라이더 미리 보기와 상관없이 그대로 보인다.
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSchedule", "WhyNow" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            system.explainNpc( npcIndex, _text );
            ImGui::TextUnformatted( _text.c_str(), _text.c_str() + _text.size() );
        }
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSchedule", "RawTimeline" ) ) )
        {
            system.dumpTimeline( npcIndex, _text );
            ImGui::TextUnformatted( _text.c_str(), _text.c_str() + _text.size() );
        }
    }

    SW_EDITOR_PANEL( ScheduleDebugPanel, "npc_schedule", EditorPanelCategory::Custom, 9310 );
} // namespace sw::editor
