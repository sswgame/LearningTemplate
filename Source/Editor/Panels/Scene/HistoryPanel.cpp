#include "pch.h"

#include "Editor/Panels/Scene/HistoryPanel.h"

#include "Core/Container/formatString.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/LogOnce.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorCompactControls.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct HistoryPanelInternal
        {
            /** @brief 지금 위치 표시입니다. 선택과 같은 뜻이라 강조색이다(앱 화면 규칙 — 강조색은 데이터 · 선택에만, 예전 고정 초록은 테마를 따르지 않았다). */
            static Color getCurrentMarker() { return SW_THEME_COLOR( "Base", "Accent" ); }
            /** @brief 되돌린(다시 실행할 수 있는) 항목의 흐린 글입니다. */
            static Color getUndoneText() { return SW_THEME_COLOR( "Base", "TextMuted" ); }

            /** @brief 위쪽 막대(실행 취소 · 다시 실행 · 지우기 · 위치 표시)를 그립니다. */
            static void drawToolbar( const HistoryViewState& state, HistoryViewModel& viewModel )
            {
                const size_t cmdCount  = state._listLabel.size();
                const size_t currIndex = state._currentIndex;
                const bool   bCanUndo  = state._bCanUndo;
                const bool   bCanRedo  = state._bCanRedo;

                // 상단 툴바: 실행 취소 · 다시 실행 · 기록 지우기. 글 단추("Undo (Ctrl+Z)")는 좁은 도크에서 잘려 아이콘 + 툴팁으로 둔다(언리얼 Undo History · 유니티 툴바와 같은 모양).
                // 막힌 단추는 왜 막혔는지 툴팁에 쓴다.
                if ( EditorChrome::beginToolbar( "##HistoryToolbar" ) )
                {
                    if ( EditorCompactControls::drawIconButton( "##historyUndo", SW_EDITOR_ICON( "Undo" ), SW_LOCKEY( "EditorScene", "HistoryUndo" ),
                                                                bCanUndo ? nullptr : SW_LOCKEY( "EditorScene", "HistoryNothingToUndo" ) ) )
                        viewModel.undo();
                    EditorSelfTestMarks::record( "history.undo" );
                    ImGui::SameLine();
                    if ( EditorCompactControls::drawIconButton( "##historyRedo", SW_EDITOR_ICON( "Redo" ), SW_LOCKEY( "EditorScene", "HistoryRedo" ),
                                                                bCanRedo ? nullptr : SW_LOCKEY( "EditorScene", "HistoryNothingToRedo" ) ) )
                        viewModel.redo();
                    EditorSelfTestMarks::record( "history.redo" );
                    ImGui::SameLine();
                    if ( EditorCompactControls::drawIconButton( "##historyClear", SW_EDITOR_ICON( "Trash" ), SW_LOCKEY( "EditorScene", "HistoryClear" ),
                                                                cmdCount > 0 ? nullptr : SW_LOCKEY( "EditorScene", "HistoryNothingToUndo" ) ) )
                        viewModel.clear();
                    EditorSelfTestMarks::record( "history.clear" );

                    ImGui::SameLine();
                    EditorWidgets::drawCountLabel( static_cast<uint32>( currIndex ), static_cast<uint32>( cmdCount ) );
                }
                EditorChrome::endToolbar();
            }

            /** @brief 명령 내역 테이블을 그립니다. 줄을 누르면 그 시점으로 이동합니다. */
            static void drawTable( const HistoryViewState& state, HistoryViewModel& viewModel )
            {
                const size_t cmdCount  = state._listLabel.size();
                const size_t currIndex = state._currentIndex;

                // 명령 히스토리 리스트 테이블
                // 줄무늬 바탕은 두지 않는다(앱 화면 규칙 — 행은 간격으로 가른다).
                if ( ImGui::BeginTable( "HistoryTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY ) )
                {
                    ImGui::TableSetupColumn( "Step", ImGuiTableColumnFlags_WidthFixed, 50.0f * EditorThemeUtil::getDPIScale() );
                    ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthFixed, 30.0f * EditorThemeUtil::getDPIScale() );
                    ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthStretch );
                    ImGui::TableHeadersRow();

                    // 0번 엔트리: 초기 상태 (Initial State)
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled( "#0" );
                    ImGui::TableNextColumn();
                    if ( currIndex == 0 )
                        ImGui::TextColored( EditorColorUtil::toImVec4( HistoryPanelInternal::getCurrentMarker() ), ">" );
                    else
                        ImGui::TextUnformatted( "" );
                    ImGui::TableNextColumn();
                    const bool bZeroSelected = ( currIndex == 0 );
                    if ( ImGui::Selectable( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EditorScene", "HistoryInitialState" ), "entry0" ), bZeroSelected, ImGuiSelectableFlags_SpanAllColumns ) )
                        viewModel.jumpTo( 0 );
                    EditorSelfTestMarks::record( "history.step.0" );

                    // 각 명령 엔트리 (1..cmdCount)
                    for ( size_t cmdIndex = 0; cmdIndex < cmdCount; ++cmdIndex )
                    {
                        const size_t stepNum       = cmdIndex + 1;
                        const bool   bIsActiveHead = ( stepNum == currIndex );
                        const bool   bIsUndone     = ( stepNum > currIndex );

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if ( bIsUndone )
                            ImGui::TextDisabled( "#%zu", stepNum );
                        else
                            ImGui::Text( "#%zu", stepNum );

                        ImGui::TableNextColumn();
                        if ( bIsActiveHead )
                            ImGui::TextColored( EditorColorUtil::toImVec4( HistoryPanelInternal::getCurrentMarker() ), ">" );
                        else
                            ImGui::TextUnformatted( "" );

                        ImGui::TableNextColumn();
                        // 명령 이름은 ImGui 라벨로 쓰지 않는다 — "##" 가 포함된 이름은 그 뒤가 숨겨진다. 라벨 없는 줄 위에 글을 따로 그린다.
                        const utf8* pCmdLabel = state._listLabel[cmdIndex].c_str(); // 이름이 없는 명령은 ViewModel 이 "Command" 로 채운다

                        if ( bIsUndone )
                            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( HistoryPanelInternal::getUndoneText() ) );

                        ImGui::PushID( static_cast<int32>( stepNum ) );
                        if ( ImGui::Selectable( "##step", bIsActiveHead, ImGuiSelectableFlags_SpanAllColumns ) )
                            viewModel.jumpTo( stepNum );
                        fixed_string<constant::buffer::kMax32> mark;
                        formatstring( mark.data(), mark.capacity(), "history.step.%#", stepNum );
                        EditorSelfTestMarks::record( mark.c_str() ); // 시나리오가 그 시점으로 되돌린다
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
        , _viewModel{}
        , _viewState{}
    {
    }

    void HistoryPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        if ( _viewState._bAvailable == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorScene", "CommandStackUnavailable" ) );
            return;
        }

        HistoryPanelInternal::drawToolbar( _viewState, _viewModel );

        ImGui::Separator();

        HistoryPanelInternal::drawTable( _viewState, _viewModel );
    }
} // namespace sw::editor
