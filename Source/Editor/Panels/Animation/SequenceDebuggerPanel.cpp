/**
 * @file SequenceDebuggerPanel.cpp
 * @brief 컷씬 디버거 — 실행 중인 시퀀스 플레이어 하나의 플레이헤드 · 활성 항목 · 바인딩 상태 · 최근 기록, 그리고 켜진 키트가 보탠 해석(실제 카메라 블렌드 가중치 등)을 읽기 전용으로 보입니다.
 * @details 시퀀서 패널은 편집 중 미리보기이고, 이 창은 재생 중인 `SequencePlayerComponent` 를 본다(언리얼 Sequencer 의 재생 중 디버그 · Rewind Debugger 의 시퀀서 트랙 자리).
 *          키트의 해석은 정적 등록 제공자(`SW_SEQUENCE_DIAGNOSTIC`)라 그 키트 모듈이 올라와 있을 때만 보이고, 내려가면 목록에서 빠진다. 기록은 플레이어마다 고정 용량이다.
 *          값은 `SequenceDebuggerViewModel::gather` 가 모으고, 이 패널은 그리기만 합니다(MVVM 계약).
 */
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Animation/Model/SequenceDebuggerViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    /** @brief 컷씬 디버거 창입니다(읽기 전용). */
    class SequenceDebuggerPanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "sequence_debugger";

        SequenceDebuggerPanel();
        virtual ~SequenceDebuggerPanel() override = default;

        const utf8* getPanelTitle() const override { return "Cutscene Debugger"; }
        void        drawContent() override;

    private:
        void drawPlayerPicker();
        void drawBindingTable();
        void drawHistoryTable();
        void drawProviders();

    private:
        SequenceDebuggerViewModel _viewModel;
        SequenceDebuggerViewState _viewState; ///< 프레임마다 다시 채운다
    };

    SW_EDITOR_PANEL( SequenceDebuggerPanel, SequenceDebuggerPanel::kPanelID, EditorPanelCategory::Tool, 1005 );
    SequenceDebuggerPanel::SequenceDebuggerPanel()
        : EditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
    {
    }

    void SequenceDebuggerPanel::drawContent()
    {
        gatherViewState( _viewModel, _viewState );
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
