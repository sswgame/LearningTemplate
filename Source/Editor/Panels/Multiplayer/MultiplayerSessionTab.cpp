#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorNetModeCombo.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

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
     */
    class MultiplayerSessionTab final : public IMultiplayerTab
    {
    public:
        MultiplayerSessionTab();

        void drawTab( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 프리셋 고르기 · 이름 붙여 저장 · 지우기 줄입니다. 고르면 그 구성을 다음 세션의 구성으로 복사합니다. */
        void drawPresets( EditorNetPlaySession& session );
        void drawControls( EditorNetPlaySession& session );
        /** @brief 창 배치(켜기 · 창 크기 · 한 줄의 창 수) 줄입니다. */
        void drawWindowLayout( EditorNetPlaySettings& settings );
        void drawProcessTable( EditorNetPlaySession& session );
        void drawLog( EditorNetPlaySession& session );

        fixed_string<constant::kMaxBuffer64>  _presetName;      ///< 저장할 프리셋 이름(편집 필드). 비어 있으면 고른 프리셋을 덮는다
        fixed_string<constant::kMaxBuffer256> _clientArguments; ///< 클라이언트마다 붙일 인자(편집 필드)
        int32                                 _stepTickCount;   ///< "Step" 이 서버를 돌릴 틱 수
        int32                                 _logSource;       ///< 로그 거르기 — -1 이면 모두, 아니면 프로세스 번호
        bool                                  _bFollowLog;      ///< 새 줄이 오면 끝으로 스크롤
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerSessionTab::MultiplayerSessionTab()
        : _presetName{}
        , _clientArguments{}
        , _stepTickCount{ 1 }
        , _logSource{ -1 }
        , _bFollowLog{ true }
    {
    }

    void MultiplayerSessionTab::drawTab( const MultiplayerTabContext& context )
    {
        EditorNetPlaySession& session = *context._pSession;
        drawPresets( session );
        drawControls( session );
        ImGui::Separator();
        drawProcessTable( session );
        ImGui::Separator();
        drawLog( session );
    }

    void MultiplayerSessionTab::drawPresets( EditorNetPlaySession& session )
    {
        EditorNetPlayPresetStore& store        = session.getPresetStore();
        const string              selectedName = store.getSelectedPresetName(); // 사본 — 아래 호출이 고른 이름을 바꾼다
        ImGui::BeginDisabled( session.isRunning() );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( "Preset" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 10.0f );
        if ( ImGui::BeginCombo( "##NetPlayPreset", selectedName.empty() ? "(none)" : selectedName.c_str() ) )
        {
            string pickedName;
            for ( const EditorNetPlayPreset& preset : store.getPresets() )
            {
                if ( ImGui::Selectable( preset._name.c_str(), preset._name == selectedName ) )
                    pickedName = preset._name;
            }
            ImGui::EndCombo();
            // 고르면 그 구성을 다음 세션의 구성으로 — 고르기를 파일에 쓰지 못했으면(오류는 저장소가 남겼다) 구성도 그대로 둔다.
            if ( pickedName.empty() == false && store.selectPreset( pickedName ) )
                session.getSettings() = *store.findSelectedPreset();
        }
        EditorSelfTestMarks::note( "multiplayer.preset" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "Preset" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 8.0f );
        ImGui::InputTextWithHint( "##NetPlayPresetName", SW_LOCKEY( "EditorMultiplayer", "PresetNameHint" ), _presetName.data(), _presetName.capacity() );
        ImGui::SameLine();
        const string saveName = _presetName.empty() ? selectedName : string( _presetName.c_str() );
        ImGui::BeginDisabled( saveName.empty() );
        if ( ImGui::Button( "Save preset" ) && store.savePreset( saveName, session.getSettings() ) )
            _presetName.clear(); // 실패 이유는 저장소가 오류로 남겼다 — 이름은 다시 누를 수 있게 둔다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SavePreset" ) );
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( selectedName.empty() );
        if ( ImGui::Button( "Delete preset" ) )
            (void)store.removePreset( selectedName ); // 실패 이유는 저장소가 오류로 남긴다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "DeletePreset" ) );
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }

    void MultiplayerSessionTab::drawWindowLayout( EditorNetPlaySettings& settings )
    {
        ImGui::Checkbox( "Arrange windows", &settings._bArrangeWindows );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ArrangeWindows" ) );
        if ( settings._bArrangeWindows == false )
            return;
        const float32 fieldWidth = ImGui::GetFontSize() * 4.0f;
        ImGui::SameLine();
        ImGui::SetNextItemWidth( fieldWidth );
        ImGui::InputInt( "##WindowWidth", &settings._windowWidth, 0, 0 );
        ImGui::SameLine();
        ImGui::TextUnformatted( "x" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( fieldWidth );
        ImGui::InputInt( "##WindowHeight", &settings._windowHeight, 0, 0 );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "WindowSize" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( fieldWidth );
        ImGui::InputInt( "##WindowColumns", &settings._windowColumnCount, 1, 1 );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "WindowColumns" ) );
        settings._windowWidth       = MathUtil::clamp( settings._windowWidth, MultiplayerSessionTabInternal::kMinWindowSide, MultiplayerSessionTabInternal::kMaxWindowSide );
        settings._windowHeight      = MathUtil::clamp( settings._windowHeight, MultiplayerSessionTabInternal::kMinWindowSide, MultiplayerSessionTabInternal::kMaxWindowSide );
        settings._windowColumnCount = MathUtil::clamp( settings._windowColumnCount, 1, EditorNetModeCombo::kMaxPlayerCount );
    }

    void MultiplayerSessionTab::drawControls( EditorNetPlaySession& session )
    {
        EditorNetPlaySettings& settings = session.getSettings();
        (void)EditorNetModeCombo::draw( session ); // 바뀐 구성은 다음 Play 에 쓰인다
        ImGui::SameLine();
        ImGui::BeginDisabled( session.isRunning() );
        ImGui::Checkbox( "Record traces", &settings._bRecordTraces );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "RecordTraces" ) );
        ImGui::SameLine();
        drawWindowLayout( settings );
        // 프리셋을 고르면 구성이 통째로 바뀐다 — 편집 칸은 늘 구성을 따른다(입력은 아래에서 곧바로 구성에 쓰므로 같은 글이다).
        if ( ( _clientArguments == settings._clientArguments.c_str() ) == false )
            _clientArguments = settings._clientArguments.c_str();
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::InputTextWithHint( "##ClientArguments", SW_LOCKEY( "EditorMultiplayer", "ClientArgumentsHint" ), _clientArguments.data(), _clientArguments.capacity() ) )
            settings._clientArguments = _clientArguments.c_str();
        ImGui::EndDisabled();

        if ( session.isRunning() == false )
        {
            ImGui::BeginDisabled( settings._mode == EditorNetMode::Standalone );
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Play" ) ) )
                (void)session.start( session.getSettings() ); // 실패 이유는 세션이 로그에 남긴다
            EditorSelfTestMarks::note( "multiplayer.play" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "Play" ) );
            ImGui::EndDisabled();
        }
        else
        {
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStop, "Stop" ) ) )
                (void)session.stop(); // 합친 트레이스는 Profiler 창의 프레임 단계가 연다. 경로는 아래 줄에도 남는다
            EditorSelfTestMarks::note( "multiplayer.stop" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "Stop" ) );
            ImGui::SameLine();
            ImGui::TextDisabled( "UDP %u", static_cast<uint32>( session.getPort() ) );
            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPause, "Pause server" ) ) )
                (void)session.sendServerCommand( "pause" ); // 답은 서버 로그 줄로 온다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "PauseServer" ) );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 3.0f );
            ImGui::InputInt( "##StepTicks", &_stepTickCount, 0, 0 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "StepTickCount" ) );
            _stepTickCount = MathUtil::clamp( _stepTickCount, 1, MultiplayerSessionTabInternal::kMaxStepTickCount );
            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStepForward, "Step" ) ) )
                (void)session.sendServerCommand( "step " + to_string( _stepTickCount ) ); // 답은 서버 로그 줄로 온다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "StepServer" ) );
            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Resume server" ) ) )
                (void)session.sendServerCommand( "resume" ); // 답은 서버 로그 줄로 온다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ResumeServer" ) );
        }
        if ( session.getMergedTracePath().empty() == false )
        {
            ImGui::TextDisabled( "Last merged trace:" );
            ImGui::SameLine();
            ImGui::TextUnformatted( session.getMergedTracePath().c_str() );
        }
    }

    void MultiplayerSessionTab::drawProcessTable( EditorNetPlaySession& session )
    {
        const int32 processCount = session.getProcessCount();
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
            const EditorNetPlayProcessStatus status = session.getProcessStatus( processIndex );
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( status._label.c_str() );
            ImGui::TableNextColumn();
            ImGui::Text( "%d", status._processID );
            ImGui::TableNextColumn();
            if ( status._bRunning )
                EditorWidgets::drawChip( "running", EditorThemeUtil::getSuccessColor() );
            else
                EditorWidgets::drawChip( ( "exited " + to_string( status._exitCode ) ).c_str(), EditorThemeUtil::getWarningColor() );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( processIndex == EditorNetPlaySession::kServerProcessIndex ? "server" : ( status._bConnected ? "connected" : "-" ) );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( status._lastLine.c_str() );
        }
        ImGui::EndTable();
    }

    void MultiplayerSessionTab::drawLog( EditorNetPlaySession& session )
    {
        // 이름은 한 번만 — 줄마다 상태 사본을 만들지 않는다.
        const int32    processCount = session.getProcessCount();
        vector<string> listLabel;
        listLabel.reserve( static_cast<size_t>( processCount ) );
        for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
        {
            listLabel.push_back( session.getProcessStatus( processIndex )._label );
        }
        if ( _logSource >= processCount )
            _logSource = -1;
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 10.0f );
        if ( ImGui::BeginCombo( "##LogSource", _logSource < 0 ? "All processes" : listLabel[static_cast<size_t>( _logSource )].c_str() ) )
        {
            if ( ImGui::Selectable( "All processes", _logSource < 0 ) )
                _logSource = -1;
            for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
            {
                if ( ImGui::Selectable( listLabel[static_cast<size_t>( processIndex )].c_str(), _logSource == processIndex ) )
                    _logSource = processIndex;
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "LogSource" ) );
        ImGui::SameLine();
        ImGui::Checkbox( "Follow", &_bFollowLog );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "FollowLog" ) );
        if ( ImGui::BeginChild( "##NetPlayLog", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders ) )
        {
            for ( const EditorNetPlayLogLine& line : session.getLogLines() )
            {
                if ( _logSource >= 0 && line._processIndex != _logSource )
                    continue;
                ImGui::TextDisabled( "[%s]", line._processIndex < processCount ? listLabel[static_cast<size_t>( line._processIndex )].c_str() : "?" );
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
