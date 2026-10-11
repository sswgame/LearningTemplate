#include "pch.h"

#include "Editor/Panels/MultiplayerPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/EditorNetPlaySession.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Console/DevCommandRegistry.h"

#include <imgui.h>
#include <chrono>
#include <thread>

namespace sw::editor
{
    SW_LOG_CALLER( "MultiplayerPanel" );

    namespace
    {
        struct MultiplayerPanelInternal
        {
            /** @brief 플레이어 수의 상한입니다(한 PC 에 클라이언트 창을 이보다 많이 띄우지 않는다 — 부하 시험은 `Tools/OnlineLoadBot`). */
            static constexpr int32 kMaxPlayerCount = 8;
            /** @brief "Step" 이 한 번에 돌릴 수 있는 서버 틱 상한입니다. */
            static constexpr int32 kMaxStepTickCount = 600;
            /** @brief `netplay.wait` 의 기본 시한(초)입니다. */
            static constexpr int32 kDefaultWaitSeconds = 60;
            /** @brief `netplay.wait` 가 세션을 다시 보는 간격입니다. */
            static constexpr int64 kWaitPollMilliseconds = 50;

            static EditorNetPlaySession* findSession()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? &pContext->getNetPlaySession() : nullptr;
            }

            /** @brief 탐침 `Editor.NetPlayProcessCount` — 돌고 있는 멀티 프로세스 재생의 프로세스 수입니다. */
            [[nodiscard]] static bool readProcessCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( pSession->getProcessCount() ) : 0.0;
                return true;
            }

            /** @brief 탐침 `Editor.NetPlayConnectedClients` — 서버에 붙었다고 알린 클라이언트 수입니다. */
            [[nodiscard]] static bool readConnectedClients( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( pSession->getConnectedClientCount() ) : 0.0;
                return true;
            }

            /** @brief `netplay.start [dedicated|listen] [<플레이어 수>]` — 구성을 고치고 세션을 시작합니다. */
            static bool runStart( const vector<string>& listArgument, string& outReply )
            {
                EditorNetPlaySession* pSession = findSession();
                if ( pSession == nullptr )
                {
                    outReply = "the editor is not running";
                    return true;
                }
                EditorNetPlaySettings& settings = pSession->getSettings();
                settings._mode                  = settings._mode == EditorNetMode::Standalone ? EditorNetMode::DedicatedServer : settings._mode;
                for ( const string& argument : listArgument )
                {
                    int32 playerCount = 0;
                    if ( argument == "dedicated" )
                        settings._mode = EditorNetMode::DedicatedServer;
                    else if ( argument == "listen" )
                        settings._mode = EditorNetMode::ListenHost;
                    else if ( StringUtil::parseInt( argument, playerCount ) && 1 <= playerCount && playerCount <= kMaxPlayerCount )
                        settings._clientCount = playerCount;
                    else
                        return false;
                }
                const bool bStarted = MultiplayerPanel::startSession( *pSession );
                outReply            = bStarted ? "multiplayer session started on UDP port " + to_string( pSession->getPort() ) : "multiplayer session did not start (see the log)";
                return true;
            }

            /** @brief `netplay.stop` — 모두 정상 종료시키고 트레이스를 합칩니다. */
            static bool runStop( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                EditorNetPlaySession* pSession = findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                const string mergedPath = pSession->stop();
                outReply                = mergedPath.empty() ? string( "multiplayer session stopped" ) : "multiplayer session stopped - merged trace " + mergedPath;
                return true;
            }

            /** @brief `netplay.wait <클라이언트 수> [<초>]` — 그만큼 붙을 때까지 이 프레임을 붙잡습니다(자동화 시나리오용 — 시나리오는 프레임 단위라 기동 시간을 모른다). */
            static bool runWait( const vector<string>& listArgument, string& outReply )
            {
                int32 clientCount = 0;
                int32 seconds     = kDefaultWaitSeconds;
                if ( listArgument.empty() || StringUtil::parseInt( listArgument[0], clientCount ) == false ||
                     ( listArgument.size() > 1 && StringUtil::parseInt( listArgument[1], seconds ) == false ) )
                    return false;
                EditorNetPlaySession* pSession = findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( seconds ) * 1000 );
                while ( pSession->getConnectedClientCount() < clientCount && deadline.isExpired() == false )
                {
                    pSession->update();
                    std::this_thread::sleep_for( std::chrono::milliseconds( kWaitPollMilliseconds ) );
                }
                outReply = "netplay connected clients " + to_string( pSession->getConnectedClientCount() ) + " of " + to_string( clientCount );
                return true;
            }

            /** @brief `netplay.server <명령>` — 서버 표준 입력에 한 줄을 보냅니다(`pause` · `step 3` · `resume` · 서버 개발 명령). */
            static bool runServer( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() )
                    return false;
                string command;
                for ( const string& argument : listArgument )
                {
                    if ( command.empty() == false )
                        command += ' ';
                    command += argument;
                }
                EditorNetPlaySession* pSession = findSession();
                const bool            bSent    = pSession != nullptr && pSession->sendServerCommand( command );
                outReply                       = bSent ? "sent to the server: " + command : string( "no server to send to" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_PROBE( editorNetPlayProcessCount, "Editor.NetPlayProcessCount", "Processes of the running multiplayer session (server and clients, 0 when stopped)",
                         &MultiplayerPanelInternal::readProcessCount );
    SW_AUTOMATION_PROBE( editorNetPlayConnectedClients, "Editor.NetPlayConnectedClients", "Clients of the multiplayer session that logged a connection to the server",
                         &MultiplayerPanelInternal::readConnectedClients );
    SW_DEV_COMMAND( NetPlayStart, "netplay.start", "netplay.start [dedicated|listen] [<players>]", "Start the editor's multi-process play session",
                    &MultiplayerPanelInternal::runStart );
    SW_DEV_COMMAND( NetPlayStop, "netplay.stop", "netplay.stop", "Stop every process of the multi-process play session and merge their traces",
                    &MultiplayerPanelInternal::runStop );
    SW_DEV_COMMAND( NetPlayWait, "netplay.wait", "netplay.wait <clients> [<seconds>]", "Block until that many clients of the multiplayer session connected",
                    &MultiplayerPanelInternal::runWait );
    SW_DEV_COMMAND( NetPlayServer, "netplay.server", "netplay.server <command>", "Send a console line (pause, step N, resume, dev commands) to the session's server",
                    &MultiplayerPanelInternal::runServer );
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerPanel::MultiplayerPanel()
        : IEditorPanel( false )
        , _clientArguments{}
        , _stepTickCount{ 1 }
        , _logSource{ -1 }
        , _bFollowLog{ true }
    {
    }

    bool MultiplayerPanel::drawNetModeCombo( EditorNetPlaySession& session )
    {
        EditorNetPlaySettings& settings = session.getSettings();
        bool                   bChanged = false;
        ImGui::BeginDisabled( session.isRunning() );
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 9.0f );
        if ( ImGui::BeginCombo( "##NetMode", toString( settings._mode ) ) )
        {
            constexpr EditorNetMode kArrMode[] = { EditorNetMode::Standalone, EditorNetMode::DedicatedServer, EditorNetMode::ListenHost };
            for ( const EditorNetMode mode : kArrMode )
            {
                if ( ImGui::Selectable( toString( mode ), settings._mode == mode ) )
                {
                    settings._mode = mode;
                    bChanged       = true;
                }
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::note( "toolbar.netMode" );
        EditorWidgets::drawTooltip( "Net Mode — Standalone 은 에디터 안에서 재생하고, 나머지는 서버와 클라이언트를 따로 띄운다 (Window > Multiplayer)" );
        if ( settings._mode != EditorNetMode::Standalone )
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 4.0f );
            if ( ImGui::InputInt( "##PlayerCount", &settings._clientCount, 1, 1 ) )
                bChanged = true;
            settings._clientCount = MathUtil::clamp( settings._clientCount, 1, MultiplayerPanelInternal::kMaxPlayerCount );
            EditorWidgets::drawTooltip( "플레이어 수 — 전용 서버면 클라이언트 수, 리슨 호스트면 호스트를 넣은 수" );
        }
        ImGui::EndDisabled();
        return bChanged;
    }

    bool MultiplayerPanel::startSession( EditorNetPlaySession& session )
    {
        return session.start( session.getSettings() ); // 실패 이유는 세션이 로그에 남긴다
    }

    void MultiplayerPanel::drawContent()
    {
        EditorNetPlaySession* pSession = MultiplayerPanelInternal::findSession();
        if ( pSession == nullptr )
            return;
        if ( ImGui::BeginTabBar( "##MultiplayerTabs" ) )
        {
            if ( ImGui::BeginTabItem( "Session" ) )
            {
                drawSessionTab( *pSession );
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void MultiplayerPanel::drawSessionTab( EditorNetPlaySession& session )
    {
        EditorNetPlaySettings& settings = session.getSettings();
        (void)drawNetModeCombo( session ); // 바뀐 구성은 다음 Play 에 쓰인다
        ImGui::SameLine();
        ImGui::BeginDisabled( session.isRunning() );
        ImGui::Checkbox( "Record traces", &settings._bRecordTraces );
        EditorWidgets::drawTooltip( "프로세스마다 타임라인을 기록해 Stop 때 한 파일(merged.trace.json)로 합칩니다 — Profiler 창 Load Trace · Perfetto 로 엽니다" );
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::InputTextWithHint( "##ClientArguments", "client arguments (e.g. -vulkan -width=960 -height=540)", _clientArguments.data(), _clientArguments.capacity() ) )
            settings._clientArguments = _clientArguments.c_str();
        ImGui::EndDisabled();

        if ( session.isRunning() == false )
        {
            ImGui::BeginDisabled( settings._mode == EditorNetMode::Standalone );
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Play" ) ) )
                (void)startSession( session ); // 실패는 로그에
            EditorSelfTestMarks::note( "multiplayer.play" );
            ImGui::EndDisabled();
        }
        else
        {
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStop, "Stop" ) ) )
                (void)session.stop(); // 합친 경로는 로그와 아래 줄에 남는다
            EditorSelfTestMarks::note( "multiplayer.stop" );
            ImGui::SameLine();
            ImGui::TextDisabled( "UDP %u", static_cast<uint32>( session.getPort() ) );
            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPause, "Pause server" ) ) )
                (void)session.sendServerCommand( "pause" ); // 답은 서버 로그 줄로 온다
            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 3.0f );
            ImGui::InputInt( "##StepTicks", &_stepTickCount, 0, 0 );
            _stepTickCount = MathUtil::clamp( _stepTickCount, 1, MultiplayerPanelInternal::kMaxStepTickCount );
            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStepForward, "Step" ) ) )
                (void)session.sendServerCommand( "step " + to_string( _stepTickCount ) ); // 답은 서버 로그 줄로 온다
            EditorWidgets::drawTooltip( "서버 게임 틱을 왼쪽 칸의 수만큼 돌리고 다시 멈춥니다 (전용 서버 Dev 명령 step N)" );
            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Resume server" ) ) )
                (void)session.sendServerCommand( "resume" ); // 답은 서버 로그 줄로 온다
        }
        if ( session.getMergedTracePath().empty() == false )
        {
            ImGui::TextDisabled( "Last merged trace:" );
            ImGui::SameLine();
            ImGui::TextUnformatted( session.getMergedTracePath().c_str() );
        }
        ImGui::Separator();
        drawProcessTable( session );
        ImGui::Separator();
        drawLog( session );
    }

    void MultiplayerPanel::drawProcessTable( EditorNetPlaySession& session )
    {
        const int32 processCount = session.getProcessCount();
        if ( processCount == 0 )
        {
            ImGui::TextDisabled( "No processes - pick a Net Mode and press Play (toolbar or here)." );
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
                EditorWidgets::drawChip( "running", editor::style::kOk );
            else
                EditorWidgets::drawChip( ( "exited " + to_string( status._exitCode ) ).c_str(), editor::style::kWarn );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( processIndex == 0 ? "server" : ( status._bConnected ? "connected" : "-" ) );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( status._lastLine.c_str() );
        }
        ImGui::EndTable();
    }

    void MultiplayerPanel::drawLog( EditorNetPlaySession& session )
    {
        // 이름표는 한 번만 — 줄마다 상태 사본을 만들지 않는다.
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
        ImGui::SameLine();
        ImGui::Checkbox( "Follow", &_bFollowLog );
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

    SW_EDITOR_PANEL( MultiplayerPanel, "multiplayer", EditorPanelCategory::Tool, 2030 );
} // namespace sw::editor
