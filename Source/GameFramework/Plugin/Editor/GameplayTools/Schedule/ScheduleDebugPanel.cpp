#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Schedule/ScheduleDebugPanel.h"

#include "Editor/Common/Commands/EditorCommandIDs.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Input/EditorCommandGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"

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
                const EditorPanelManager* pManager = EditorPanelManager::findInEditorContext();
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
        , _state{}
    {
    }

    void ScheduleDebugPanel::drawContent()
    {
        _model.gather( _state );
        if ( _state._bHasSystem == SW_FALSE )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdSchedule", "EmptyHint" ) );
            if ( _state._bPlayStopped != SW_FALSE )
                (void)EditorCommandGUI::drawCommandButton( editorcommand::kPlayStart ); // 다음 할 일 단추(Ability Debugger 와 같은 자리)
            return;
        }
        if ( _state._systemCount > 1 )
        {
            int32 systemIndex = _state._systemIndex;
            EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdSchedule", "SystemIndex" ) ); // 이름표는 칸 앞에(칸에 주면 오른쪽에 붙는다)
            EditorChrome::setNextItemWidthColumns( 16.0f );
            if ( EditorWidgets::inputInt( SW_WIDGET_ID( "##SystemIndex" ), &systemIndex ) )
                _model.selectSystem( systemIndex ); // 다음 프레임부터 그 시스템을 보인다
        }

        if ( ImGui::BeginChild( "##ScheduleNPCList", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 27.0f, 0.0f ) ), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX ) )
            drawNPCList();
        ImGui::EndChild();
        ImGui::SameLine();

        if ( ImGui::BeginChild( "##ScheduleDetail" ) )
        {
            if ( _state._selectedNPC.has_value() == false )
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdSchedule", "SelectNPCHint" ) );
            }
            else
            {
                drawTimeline();
                drawExplanation();
            }
        }
        ImGui::EndChild();
    }

    void ScheduleDebugPanel::drawNPCList()
    {
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdSchedule", "NPCListTitle" ) );
        ImGui::Separator();
        for ( size_t npcIndex = 0; npcIndex < _state._listNPC.size(); ++npcIndex )
        {
            const string& npcID     = _state._listNPC[npcIndex];
            const bool    bSelected = _state._selectedNPC.has_value() && *_state._selectedNPC == static_cast<int32>( npcIndex );
            ImGui::PushID( static_cast<int32>( npcIndex ) );
            if ( ImGui::Selectable( npcID.c_str(), bSelected ) )
                _model.selectNPC( npcID );
            EditorSelfTestMarks::recordFormat( "schedule.npc.%#", npcID ); // 자동화가 NPC 를 고를 자리
            ImGui::PopID();
        }
    }

    void ScheduleDebugPanel::drawTimeline()
    {
        bool bFollowClock = _state._bFollowClock != SW_FALSE;
        if ( ImGui::Checkbox( SW_LOCKEY( "EdSchedule", "FollowClock" ), &bFollowClock ) )
            _model.setFollowClock( bFollowClock );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -1.0f );
        int32        previewMinute = _state._previewMinute;
        const string clockText     = ScheduleTimelineRows::formatClock( previewMinute );
        if ( EditorWidgets::sliderInt( SW_WIDGET_ID( "##SchedulePreviewMinute" ), &previewMinute, 0, kScheduleMinutesPerDay - 1, clockText.c_str() ) )
            _model.setPreviewMinute( previewMinute );
        ImGui::TextDisabled( "%s %s (day %d)", SW_LOCKEY( "EdSchedule", "GameTime" ), ScheduleTimelineRows::formatClock( _state._nowMinute ).c_str(), _state._day );

        const vector<ScheduleTimelineRow>& listRow = _state._listRow;
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

    void ScheduleDebugPanel::drawExplanation()
    {
        // 글은 지금 시각 기준이다 — 슬라이더 미리 보기와 상관없이 그대로 보인다. 머리의 열림을 모델에 알려 닫힌 글은 짓지 않는다.
        const bool bExplanationShown = ImGui::CollapsingHeader( SW_LOCKEY( "EdSchedule", "WhyNow" ), ImGuiTreeNodeFlags_DefaultOpen );
        if ( bExplanationShown )
            ImGui::TextUnformatted( _state._explanation.c_str(), _state._explanation.c_str() + _state._explanation.size() );
        const bool bRawTimelineShown = ImGui::CollapsingHeader( SW_LOCKEY( "EdSchedule", "RawTimeline" ) );
        if ( bRawTimelineShown )
            ImGui::TextUnformatted( _state._rawTimeline.c_str(), _state._rawTimeline.c_str() + _state._rawTimeline.size() );
        _model.setTextShown( bExplanationShown, bRawTimelineShown );
    }

    SW_EDITOR_PANEL( ScheduleDebugPanel, ScheduleDebugPanel::kPanelID, EditorPanelCategory::Custom, 9310 );

    SW_AUTOMATION_WATCH( editorScheduleNPCCount, "Editor.ScheduleNPCCount", "NPCs of the schedule system the NPC Schedule window shows",
                         &ScheduleDebugPanelInternal::readNPCCount );
    SW_AUTOMATION_WATCH( editorScheduleTimelineRowCount, "Editor.ScheduleTimelineRowCount", "Rows of today's timeline for the NPC picked in the NPC Schedule window",
                         &ScheduleDebugPanelInternal::readTimelineRowCount );
} // namespace sw::editor
