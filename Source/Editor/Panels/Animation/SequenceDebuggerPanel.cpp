#include "pch.h"

#include "Editor/Panels/Animation/SequenceDebuggerPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( SequenceDebuggerPanel, SequenceDebuggerPanel::kPanelID, EditorPanelCategory::Tool, 1005 );
    SequenceDebuggerPanel::SequenceDebuggerPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
    {
    }

    void SequenceDebuggerPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        if ( _viewState._listPlayer.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequenceDebugger", "NoPlayersHint" ) );
            return;
        }
        drawPlayerPicker();
        if ( _viewState._bHasWatched == SW_FALSE )
            return;

        // 플레이헤드: 프레임 · 범위 · 상태(재생 · 정지)입니다. 읽기 전용이다 — 고치려면 시퀀서 패널을 쓴다.
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EditorSequenceDebugger", "PlayheadFormat",
                                                 TextArgumentList()
                                                     .addInteger( "frame", _viewState._currentFrame )
                                                     .addInteger( "frameMin", _viewState._frameMin )
                                                     .addInteger( "frameMax", _viewState._frameMax )
                                                     .addText( "state", _viewState._bPlaying != SW_FALSE ? "playing" : "stopped" ) )
                                    .c_str() );
        const float32 span = static_cast<float32>( MathUtil::max( _viewState._frameMax - _viewState._frameMin, 1 ) );
        ImGui::ProgressBar( static_cast<float32>( _viewState._currentFrame - _viewState._frameMin ) / span, ImVec2( -1.0f, 0.0f ), "" );

        ImGui::SeparatorText( SW_LOCKEY( "EditorSequenceDebugger", "ActiveSection" ) );
        if ( _viewState._listActiveItem.empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequenceDebugger", "NoActiveItemsHint" ) );
        for ( const string& item : _viewState._listActiveItem )
        {
            ImGui::BulletText( "%s", item.c_str() );
        }

        drawBindingTable();
        drawHistoryTable();
        drawProviders();
    }

    void SequenceDebuggerPanel::drawPlayerPicker()
    {
        ImGui::SetNextItemWidth( -1.0f );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EditorSequenceDebugger", "PlayerLabel" ), "Player" ), _viewState._watchedName.c_str() ) )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( _viewState._listPlayer.size() ); ++index )
            {
                const SequenceDebuggerPlayerRow& row = _viewState._listPlayer[index];
                ImGui::PushID( static_cast<int32>( row._objectID ) );
                if ( ImGui::Selectable( row._name.c_str(), index == _viewState._watchedIndex ) )
                    _viewModel.watchPlayer( row._handle );
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "sequencedebugger.player" );
    }

    void SequenceDebuggerPanel::drawBindingTable()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EditorSequenceDebugger", "BindingsSection" ) );
        if ( _viewState._listBinding.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequenceDebugger", "NoBindingsHint" ) );
            return;
        }
        if ( ImGui::BeginTable( "##sequence_bindings", 3, ImGuiTableFlags_SizingStretchProp ) == false )
            return;
        ImGui::TableSetupColumn( "Id" );
        ImGui::TableSetupColumn( "Binding" );
        ImGui::TableSetupColumn( "Object" );
        ImGui::TableHeadersRow();
        for ( const SequenceDebuggerBindingRow& row : _viewState._listBinding )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text( "%u", row._id );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( row._name.c_str() );
            ImGui::TableNextColumn();
            switch ( row._state )
            {
                case SequenceDebuggerBindingState::Bound:
                {
                    ImGui::TextUnformatted( row._objectName.c_str() );
                    break;
                }
                case SequenceDebuggerBindingState::NotSpawnedYet:
                {
                    ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorSequenceDebugger", "NotSpawnedYet" ) );
                    break;
                }
                case SequenceDebuggerBindingState::Unbound:
                {
                    ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEY( "EditorSequenceDebugger", "Unbound" ) );
                    break;
                }
            }
        }
        ImGui::EndTable();
    }

    void SequenceDebuggerPanel::drawHistoryTable()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EditorSequenceDebugger", "HistorySection" ) );
        if ( _viewState._listHistory.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequenceDebugger", "EmptyHistoryHint" ) );
            return;
        }
        if ( ImGui::BeginTable( "##sequence_history", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                                EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 10.0f ) ) ) == false )
            return;
        ImGui::TableSetupColumn( "Frame" );
        ImGui::TableSetupColumn( "Kind" );
        ImGui::TableSetupColumn( "What" );
        ImGui::TableHeadersRow();
        for ( const SequenceDebuggerHistoryRow& row : _viewState._listHistory )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text( "%d", row._frame );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( row._pKindLabel );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( row._what.c_str() );
        }
        ImGui::EndTable();
        EditorSelfTestMarks::record( "sequencedebugger.history" );
    }

    void SequenceDebuggerPanel::drawProviders()
    {
        for ( const SequenceDebuggerProviderRow& row : _viewState._listProvider )
        {
            ImGui::SeparatorText( row._name.c_str() );
            ImGui::TextWrapped( "%s", row._text.c_str() );
        }
    }
} // namespace sw::editor
