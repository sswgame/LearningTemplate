#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorNetModeCombo.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MultiplayerSessionTabInternal
        {
            /** @brief "Step" 이 한 번에 돌릴 수 있는 서버 틱 상한입니다. */
            static constexpr int32 kMaxStepTickCount = 600;
            /** @brief 창 배치 크기 입력의 범위(픽셀)입니다. 아래는 `EditorNetPlaySettings` 의 `Min` 과 같다. */
            static constexpr int32 kMinWindowSide = 64;
            static constexpr int32 kMaxWindowSide = 7680;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MultiplayerSessionTab
     * @brief Session 탭 — 사용자 프리셋, 구성(Net Mode · 트레이스 · 클라이언트 인자 · 창 배치), Play · Stop, 서버 일시정지 · 한 틱 진행, 프로세스 목록,
     *        출처별 로그(서버 디버깅 S1 · S2).
     * @details 세션 값은 창이 모은 `MultiplayerSessionViewState` 에서 읽고, 바꾸는 일은 `MultiplayerSessionViewModel` 의 명령으로 한다(MVVM 계약).
     *          구성은 사본(`_settings`)을 고쳐 바뀐 프레임에만 돌려준다.
     */
    class MultiplayerSessionTab final : public IMultiplayerTab
    {
    public:
        MultiplayerSessionTab();

        void drawTab( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 프리셋 고르기 · 이름 붙여 저장 · 지우기 줄입니다. 고르면 그 구성을 다음 세션의 구성으로 복사합니다. */
        void drawPresets( MultiplayerSessionViewModel& viewModel, const MultiplayerSessionViewState& state );
        void drawControls( MultiplayerSessionViewModel& viewModel, const MultiplayerSessionViewState& state );
        /** @brief 창 배치(켜기 · 창 크기 · 한 줄의 창 수) 줄입니다. 값이 바뀌었으면(입력 · 범위 맞추기) true 입니다. */
        bool drawWindowLayout();
        void drawProcessTable( const MultiplayerSessionViewState& state );
        void drawLog( MultiplayerSessionViewModel& viewModel, const MultiplayerSessionViewState& state );

    private:
        EditorNetPlaySettings                   _settings;        ///< 이 프레임에 고치는 구성 사본(모은 값에서 복사한다 — 저장 공간을 다시 쓴다)
        vector<MultiplayerLogRow>               _listLogRow;      ///< 거른 로그 줄(보일 때만 모은다)
        fixed_string<constant::buffer::kMax64>  _presetName;      ///< 저장할 프리셋 이름(편집 필드). 비어 있으면 고른 프리셋을 덮는다
        fixed_string<constant::buffer::kMax256> _clientArguments; ///< 클라이언트마다 붙일 인자(편집 필드)
        int32                                   _stepTickCount;   ///< "Step" 이 서버를 돌릴 틱 수
        int32                                   _logSource;       ///< 로그 거르기 — -1 이면 모두, 아니면 프로세스 번호
        bool                                    _bFollowLog;      ///< 새 줄이 오면 끝으로 스크롤 NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerSessionTab::MultiplayerSessionTab()
        : _settings{}
        , _listLogRow{}
        , _presetName{}
        , _clientArguments{}
        , _stepTickCount{ 1 }
        , _logSource{ -1 }
        , _bFollowLog{ true }
    {
    }

    void MultiplayerSessionTab::drawTab( const MultiplayerTabContext& context )
    {
        MultiplayerSessionViewModel&       viewModel = *context._pSession;
        const MultiplayerSessionViewState& state     = *context._pState;
        drawPresets( viewModel, state );
        drawControls( viewModel, state );
        ImGui::Separator();
        drawProcessTable( state );
        ImGui::Separator();
        drawLog( viewModel, state );
    }

    void MultiplayerSessionTab::drawPresets( MultiplayerSessionViewModel& viewModel, const MultiplayerSessionViewState& state )
    {
        const string& selectedName = state._selectedPresetName; // 모은 값 — 아래 명령이 고른 이름을 바꿔도 이 프레임에는 그대로다
        ImGui::BeginDisabled( state._bRunning != SW_FALSE );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Preset" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" ) );
        if ( ImGui::BeginCombo( "##NetPlayPreset", selectedName.empty() ? "(none)" : selectedName.c_str() ) )
        {
            string pickedName;
            for ( const string& presetName : state._listPresetName )
            {
                if ( ImGui::Selectable( presetName.c_str(), presetName == selectedName ) )
                    pickedName = presetName;
            }
            ImGui::EndCombo();
            if ( pickedName.empty() == false )
                viewModel.selectPreset( pickedName );
        }
        EditorSelfTestMarks::record( "multiplayer.preset" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "Preset" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldComboEm" ) );
        ImGui::InputTextWithHint( "##NetPlayPresetName", SW_LOCKEY( "EditorMultiplayer", "PresetNameHint" ), _presetName.data(), _presetName.capacity() );
        ImGui::SameLine();
        const string saveName = _presetName.empty() ? selectedName : string( _presetName.c_str() );
        ImGui::BeginDisabled( saveName.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "SavePreset" ) ) && viewModel.savePreset( saveName ) )
            _presetName.clear(); // 실패 이유는 저장소가 오류로 남겼다 — 이름은 다시 누를 수 있게 둔다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SavePreset" ) );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "DeletePreset" ) ) );
        ImGui::BeginDisabled( selectedName.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "DeletePreset" ) ) )
            viewModel.removePreset( selectedName );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "DeletePreset" ) );
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }

    bool MultiplayerSessionTab::drawWindowLayout()
    {
        EditorNetPlaySettings& settings = _settings;
        bool                   bChanged = ImGui::Checkbox( SW_LOCKEY( "EditorMultiplayer", "ArrangeWindowsLabel" ), &settings._bArrangeWindows );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ArrangeWindows" ) );
        if ( settings._bArrangeWindows == false )
            return bChanged;
        const float32 fieldWidth = ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldNumberEm" );
        EditorChrome::sameLineIfFits( fieldWidth * 2.0f + ImGui::CalcTextSize( "x" ).x + ImGui::GetStyle().ItemSpacing.x * 2.0f );
        ImGui::SetNextItemWidth( fieldWidth );
        bChanged |= ImGui::InputInt( "##WindowWidth", &settings._windowWidth, 0, 0 );
        ImGui::SameLine();
        ImGui::TextUnformatted( "x" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( fieldWidth );
        bChanged |= ImGui::InputInt( "##WindowHeight", &settings._windowHeight, 0, 0 );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "WindowSize" ) );
        // 단추가 붙은 칸은 `measureStepperInput` 폭 — 숫자 칸 폭만 주면 숫자가 몇 픽셀로 눌려 ':' 만 보였다(uiaudit 31). 이름표로 무엇을 세는지 밝힌다.
        const utf8* const pColumns     = SW_LOCKEY( "EditorMultiplayer", "WindowColumnsLabel" );
        const ImGuiStyle& style        = ImGui::GetStyle();
        const float32     stepperWidth = EditorChrome::measureStepperInput( 3.0f );
        EditorChrome::sameLineIfFits( ImGui::CalcTextSize( pColumns ).x + style.ItemSpacing.x + stepperWidth );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( pColumns );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( stepperWidth );
        bChanged |= ImGui::InputInt( "##WindowColumns", &settings._windowColumnCount, 1, 1 );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "WindowColumns" ) );
        // 범위 밖 값(입력 · 프리셋)은 그리는 프레임마다 맞춘다 — 맞춘 값도 구성으로 돌려준다.
        const int32 width   = MathUtil::clamp( settings._windowWidth, MultiplayerSessionTabInternal::kMinWindowSide, MultiplayerSessionTabInternal::kMaxWindowSide );
        const int32 height  = MathUtil::clamp( settings._windowHeight, MultiplayerSessionTabInternal::kMinWindowSide, MultiplayerSessionTabInternal::kMaxWindowSide );
        const int32 columns = MathUtil::clamp( settings._windowColumnCount, 1, EditorNetModeCombo::kMaxPlayerCount );
        bChanged |= width != settings._windowWidth || height != settings._windowHeight || columns != settings._windowColumnCount;
        settings._windowWidth       = width;
        settings._windowHeight      = height;
        settings._windowColumnCount = columns;
        return bChanged;
    }

    void MultiplayerSessionTab::drawControls( MultiplayerSessionViewModel& viewModel, const MultiplayerSessionViewState& state )
    {
        _settings                       = state._settings;
        EditorNetPlaySettings& settings = _settings;
        const bool             bRunning = state._bRunning != SW_FALSE;
        bool                   bChanged = EditorNetModeCombo::draw( settings, bRunning, "multiplayer.netMode" ); // 바뀐 구성은 다음 Play 에 쓰인다
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "RecordTraces" ) ) );
        ImGui::BeginDisabled( bRunning );
        bChanged |= ImGui::Checkbox( SW_LOCKEY( "EdLabel", "RecordTraces" ), &settings._bRecordTraces );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "RecordTraces" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EditorMultiplayer", "ArrangeWindowsLabel" ) ) );
        bChanged |= drawWindowLayout();
        // 프리셋을 고르면 구성이 통째로 바뀐다 — 편집 칸은 늘 구성을 따른다(입력은 아래에서 곧바로 구성에 쓰므로 같은 글이다).
        if ( ( _clientArguments == settings._clientArguments.c_str() ) == false )
            _clientArguments = settings._clientArguments.c_str();
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::InputTextWithHint( "##ClientArguments", SW_LOCKEY( "EditorMultiplayer", "ClientArgumentsHint" ), _clientArguments.data(), _clientArguments.capacity() ) )
        {
            settings._clientArguments = _clientArguments.c_str();
            bChanged                  = true;
        }
        ImGui::EndDisabled();
        if ( bChanged )
            viewModel.setSettings( settings );

        if ( bRunning == false )
        {
            ImGui::BeginDisabled( settings._mode == EditorNetMode::Standalone || state._bStopping != SW_FALSE ); // 앞 세션을 거두는 동안은 `start` 가 거절한다
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Play" ), SW_LOCKEY( "EdVerb", "Play" ), "Play" ) ) )
                viewModel.start(); // 실패 이유는 세션이 로그에 남긴다
            EditorSelfTestMarks::record( "multiplayer.play" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "Play" ) );
            ImGui::EndDisabled();
        }
        else
        {
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Stop" ), SW_LOCKEY( "EdVerb", "Stop" ), "Stop" ) ) )
                viewModel.requestStop(); // 합친 트레이스는 Profiler 창의 프레임 단계가 연다. 경로는 아래 줄에도 남는다
            EditorSelfTestMarks::record( "multiplayer.stop" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "Stop" ) );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "UDP", TextArgumentList().addInteger( "port", static_cast<int64>( state._port ) ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Pause" ), SW_LOCKEY( "EdButton", "PauseServer" ), "Pause server" ) ) );
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Pause" ), SW_LOCKEY( "EdButton", "PauseServer" ), "Pause server" ) ) )
                (void)viewModel.sendServerCommand( "pause" ); // 답은 서버 로그 줄로 온다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "PauseServer" ) );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldTinyEm" ) );
            ImGui::InputInt( "##StepTicks", &_stepTickCount, 0, 0 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "StepTickCount" ) );
            _stepTickCount = MathUtil::clamp( _stepTickCount, 1, MultiplayerSessionTabInternal::kMaxStepTickCount );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "StepForward" ), SW_LOCKEY( "EdButton", "Step" ), "Step" ) ) );
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "StepForward" ), SW_LOCKEY( "EdButton", "Step" ), "Step" ) ) )
            {
                fixed_string<constant::buffer::kMax32> command;
                formatstring( command.data(), command.capacity(), "step %#", _stepTickCount );
                (void)viewModel.sendServerCommand( command.view() ); // 답은 서버 로그 줄로 온다
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "StepServer" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Play" ), SW_LOCKEY( "EdButton", "ResumeServer" ), "Resume server" ) ) );
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Play" ), SW_LOCKEY( "EdButton", "ResumeServer" ), "Resume server" ) ) )
                (void)viewModel.sendServerCommand( "resume" ); // 답은 서버 로그 줄로 온다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ResumeServer" ) );
        }
        if ( state._mergedTracePath.empty() == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "LastMergedTrace" ) );
            ImGui::SameLine();
            ImGui::TextUnformatted( state._mergedTracePath.c_str() );
        }
    }

    void MultiplayerSessionTab::drawProcessTable( const MultiplayerSessionViewState& state )
    {
        const int32 processCount = static_cast<int32>( state._listProcess.size() );
        if ( processCount == 0 )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "NoProcesses" ) );
            return;
        }
        if ( ImGui::BeginTable( "##NetPlayProcesses", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp ) == false )
            return;
        ImGui::TableSetupColumn( "Process", ImGuiTableColumnFlags_WidthFixed );
        ImGui::TableSetupColumn( "PID", ImGuiTableColumnFlags_WidthFixed );
        ImGui::TableSetupColumn( "State", ImGuiTableColumnFlags_WidthFixed );
        ImGui::TableSetupColumn( "Net", ImGuiTableColumnFlags_WidthFixed );
        ImGui::TableSetupColumn( "Last line" );
        ImGui::TableHeadersRow();
        for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
        {
            const MultiplayerProcessRow& status = state._listProcess[static_cast<size_t>( processIndex )];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( status._label.c_str() );
            ImGui::TableNextColumn();
            ImGui::Text( "%d", status._processID );
            ImGui::TableNextColumn();
            if ( status._bRunning != SW_FALSE )
                EditorWidgets::drawChip( "running", SW_THEME_COLOR( "Signal", "Success" ) );
            else
            {
                // 기다리지 못한 프로세스는 종료 코드가 없다 — 0 으로 꾸미지 않고 따로 보인다(까닭은 그 프로세스의 마지막 출력 줄)
                fixed_string<constant::buffer::kMax64> text;
                if ( status._exitCode.has_value() )
                    formatstring( text.data(), text.capacity(), "exited %#", *status._exitCode );
                else
                    formatstring( text.data(), text.capacity(), "stopped, not waited for" );
                EditorWidgets::drawChip( text.c_str(), SW_THEME_COLOR( "Signal", "Warning" ) );
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( processIndex == MultiplayerSessionViewModel::kServerProcessIndex ? "server" : ( status._bConnected != SW_FALSE ? "connected" : "-" ) );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( status._lastLine.c_str() );
        }
        ImGui::EndTable();
    }

    void MultiplayerSessionTab::drawLog( MultiplayerSessionViewModel& viewModel, const MultiplayerSessionViewState& state )
    {
        const int32 processCount = static_cast<int32>( state._listProcess.size() );
        if ( _logSource >= processCount )
            _logSource = -1;
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" ) );
        if ( ImGui::BeginCombo( "##LogSource", _logSource < 0 ? "All processes" : state._listProcess[static_cast<size_t>( _logSource )]._label.c_str() ) )
        {
            if ( ImGui::Selectable( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "AllProcesses" ), "All processes" ), _logSource < 0 ) )
                _logSource = -1;
            for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
            {
                if ( ImGui::Selectable( state._listProcess[static_cast<size_t>( processIndex )]._label.c_str(), _logSource == processIndex ) )
                    _logSource = processIndex;
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "LogSource" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Follow" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Follow" ), &_bFollowLog );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "FollowLog" ) );
        viewModel.gatherLog( _logSource, _listLogRow );
        if ( ImGui::BeginChild( "##NetPlayLog", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders ) )
        {
            for ( const MultiplayerLogRow& line : _listLogRow )
            {
                ImGui::TextDisabled( "[%s]", line._processIndex < processCount ? state._listProcess[static_cast<size_t>( line._processIndex )]._label.c_str() : "?" );
                ImGui::SameLine();
                ImGui::TextUnformatted( line._text.c_str() );
            }
            if ( _bFollowLog && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeight() )
                ImGui::SetScrollHereY( 1.0f );
        }
        ImGui::EndChild();
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerSessionTab, "session", 100, "Session" );
} // namespace sw::editor
