#include "pch.h"

#include "Editor/Panels/Scene/HistoryPanel.h"

#include "Core/Common/Defines.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Scene/CommandStack.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct HistoryPanelInternal
        {
            static constexpr Color kCurrentMarker{ 0.2f, 0.8f, 0.3f, 1.0f };
            static constexpr Color kUndoneText{ 0.5f, 0.5f, 0.5f, 1.0f };

            /** @brief 위쪽 막대(실행 취소 · 다시 실행 · 지우기 · 위치 표시)를 그립니다. */
            static void drawToolbar( CommandStack& cmdStack )
            {
                const size_t cmdCount  = cmdStack.getCommandCount();
                const size_t currIndex = cmdStack.getCurrentIndex();
                const bool   bCanUndo  = cmdStack.canUndo();
                const bool   bCanRedo  = cmdStack.canRedo();

                // 상단 툴바: 실행 취소, 다시 실행, 히스토리 지우기
                if ( EditorChrome::beginToolbar( "##HistoryToolbar" ) )
                {
                    if ( bCanUndo == false )
                        ImGui::BeginDisabled();
                    if ( ImGui::Button( "Undo (Ctrl+Z)" ) )
                        cmdStack.undo();
                    if ( bCanUndo == false )
                        ImGui::EndDisabled();
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "HistoryUndo" ) );

                    ImGui::SameLine();
                    if ( bCanRedo == false )
                        ImGui::BeginDisabled();
                    if ( ImGui::Button( "Redo (Ctrl+Y)" ) )
                        cmdStack.redo();
                    if ( bCanRedo == false )
                        ImGui::EndDisabled();
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "HistoryRedo" ) );

                    ImGui::SameLine();
                    if ( ImGui::Button( "Clear" ) )
                        cmdStack.clear();
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "HistoryClear" ) );

                    ImGui::SameLine();
                    EditorWidgets::drawCountLabel( static_cast<uint32>( currIndex ), static_cast<uint32>( cmdCount ) );
                }
                EditorChrome::endToolbar();
            }

            /** @brief 명령 내역 테이블을 그립니다. 줄을 누르면 그 시점으로 이동합니다. */
            static void drawTable( CommandStack& cmdStack )
            {
                const size_t cmdCount  = cmdStack.getCommandCount();
                const size_t currIndex = cmdStack.getCurrentIndex();

                // 명령 히스토리 리스트 테이블
                if ( ImGui::BeginTable( "HistoryTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY ) )
                {
                    ImGui::TableSetupColumn( "Step", ImGuiTableColumnFlags_WidthFixed, 50.0f * EditorThemeUtil::getDpiScale() );
                    ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthFixed, 30.0f * EditorThemeUtil::getDpiScale() );
                    ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthStretch );
                    ImGui::TableHeadersRow();

                    // 0번 엔트리: 초기 상태 (Initial State)
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled( "#0" );
                    ImGui::TableNextColumn();
                    if ( currIndex == 0 )
                        ImGui::TextColored( EditorColorUtil::toImVec4( HistoryPanelInternal::kCurrentMarker ), ">" );
                    else
                        ImGui::TextUnformatted( "" );
                    ImGui::TableNextColumn();
                    const bool bZeroSelected = ( currIndex == 0 );
                    if ( ImGui::Selectable( "[Initial State]##entry0", bZeroSelected, ImGuiSelectableFlags_SpanAllColumns ) )
                        cmdStack.jumpTo( 0 );

                    // 각 명령 엔트리 (1..cmdCount)
                    for ( size_t cmdIndex = 0; cmdIndex < cmdCount; ++cmdIndex )
                    {
                        const CommandStack::Command& cmd           = cmdStack.getCommand( cmdIndex );
                        const size_t                 stepNum       = cmdIndex + 1;
                        const bool                   bIsActiveHead = ( stepNum == currIndex );
                        const bool                   bIsUndone     = ( stepNum > currIndex );

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if ( bIsUndone )
                            ImGui::TextDisabled( "#%zu", stepNum );
                        else
                            ImGui::Text( "#%zu", stepNum );

                        ImGui::TableNextColumn();
                        if ( bIsActiveHead )
                            ImGui::TextColored( EditorColorUtil::toImVec4( HistoryPanelInternal::kCurrentMarker ), ">" );
                        else
                            ImGui::TextUnformatted( "" );

                        ImGui::TableNextColumn();
                        // 명령 이름은 ImGui 라벨로 쓰지 않는다 — "##" 가 포함된 이름은 그 뒤가 숨겨진다. 라벨 없는 줄 위에 글을 따로 그린다.
                        const utf8* pCmdLabel = cmd._label.empty() == false ? cmd._label.c_str() : "Command";

                        if ( bIsUndone )
                            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( HistoryPanelInternal::kUndoneText ) );

                        ImGui::PushID( static_cast<int32>( stepNum ) );
                        if ( ImGui::Selectable( "##step", bIsActiveHead, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap ) )
                            cmdStack.jumpTo( stepNum );
                        ImGui::SameLine( 0.0f, 0.0f );
                        ImGui::TextUnformatted( pCmdLabel );
                        ImGui::PopID();

                        if ( bIsUndone )
                            ImGui::PopStyleColor();
                    }

                    ImGui::EndTable();
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( HistoryPanel, HistoryPanel::kPanelID, EditorPanelCategory::Tool, 700 );

    HistoryPanel::HistoryPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
    {
    }

    void HistoryPanel::drawContent()
    {
        CommandStack* pCmdStack = editor::findSubsystem<CommandStack>();
        if ( pCmdStack == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorScene", "CommandStackUnavailable" ) );
            return;
        }

        HistoryPanelInternal::drawToolbar( *pCmdStack );

        ImGui::Separator();

        HistoryPanelInternal::drawTable( *pCmdStack );
    }
} // namespace sw::editor
