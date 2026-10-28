#include "pch.h"

#include "GameFramework/PlugIn/Editor/Base/Schedule/ScheduleDebugPanel.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorTextExtentImGui.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ScheduleDebugPanelInternal
        {
            /** @brief 탐침이 읽는 값 하나입니다 — 패널 인스턴스가 게시한 값 중 @p pField 를 @p outValue 에 씁니다. 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readPublished( uint32 ScheduleDebugPanel::Published::* pField, float64& outValue )
            {
                const EditorPanelManager* pManager = editor::findSubsystem<EditorPanelManager>();
                const ScheduleDebugPanel* pPanel   = pManager != nullptr ? pManager->findPanel<ScheduleDebugPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPublished().*pField );
                return true;
            }

            [[nodiscard]] static bool readNpcCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &ScheduleDebugPanel::Published::_npcCount, outValue );
            }
            [[nodiscard]] static bool readTimelineRowCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &ScheduleDebugPanel::Published::_timelineRowCount, outValue );
            }
        };
    } // namespace
} // namespace sw::editor

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
        , _published{}
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
        Published& published = _published;
        published            = {};
        if ( listActive.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdSchedule", "EmptyHint" ) );
            return;
        }
        if ( _systemIndex < 0 || _systemIndex >= static_cast<int32>( listActive.size() ) )
            _systemIndex = 0;
        if ( listActive.size() > 1 )
        {
            EditorChrome::setNextItemWidthColumns( 16.0f );
            ImGui::InputInt( SW_LOCKEY( "EdSchedule", "SystemIndex" ), &_systemIndex );
            if ( _systemIndex < 0 || _systemIndex >= static_cast<int32>( listActive.size() ) )
                _systemIndex = 0;
        }
        const ScheduleSystem& system = *listActive[static_cast<size_t>( _systemIndex )];
        published._npcCount          = static_cast<uint32>( system.getNpcCount() );

        if ( ImGui::BeginChild( "##ScheduleNpcList", measureExtent( EditorTextExtent::ofText( 27.0f, 0.0f ) ), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX ) )
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
                published._timelineRowCount = static_cast<uint32>( _listRow.size() );
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
            EditorSelfTestMarks::note( ( string( "schedule.npc." ) + npcID.c_str() ).c_str() ); // 자동화가 NPC 를 고를 자리
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
        if ( ImGui::BeginTable( "##ScheduleTimeline", 5, flags, measureExtent( EditorTextExtent::ofText( 0.0f, 14.0f ) ) ) )
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

    SW_EDITOR_PANEL( ScheduleDebugPanel, ScheduleDebugPanel::kPanelID, EditorPanelCategory::Custom, 9310 );

    SW_AUTOMATION_WATCH( editorScheduleNpcCount, "Editor.ScheduleNpcCount", "NPCs of the schedule system the NPC Schedule window shows",
                         &ScheduleDebugPanelInternal::readNpcCount );
    SW_AUTOMATION_WATCH( editorScheduleTimelineRowCount, "Editor.ScheduleTimelineRowCount", "Rows of today's timeline for the NPC picked in the NPC Schedule window",
                         &ScheduleDebugPanelInternal::readTimelineRowCount );
} // namespace sw::editor
