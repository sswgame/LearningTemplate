#include "pch.h"

#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Widgets/EditorNetModeCombo.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"

namespace sw::editor
{
    static_assert( MultiplayerSessionViewModel::kServerProcessIndex == EditorNetPlaySession::kServerProcessIndex, "the view model's server index follows the session" );

    namespace
    {
        struct MultiplayerSessionViewModelInternal
        {
            /** @brief `netplay.start` 의 씬 인자 머리입니다(`scene=game/…/a.scene.xml`). */
            static constexpr string_view kSceneArgumentPrefix = "scene=";
            /** @brief `netplay.wait` 의 기본 시한(초)입니다. */
            static constexpr int32 kDefaultWaitSeconds = 60;

            /** @brief 감시 값 `Editor.NetPlayProcessCount` — 돌고 있는 멀티 프로세스 재생의 프로세스 수입니다. */
            [[nodiscard]] static bool readProcessCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( pSession->getProcessCount() ) : 0.0;
                return true;
            }

            /**
             * @brief 감시 값 `Editor.NetPlayObjectHashReports` — 세션의 프로세스들에게서 받은 `state.objectline` 답의 합입니다.
             * @details 답은 다른 프로세스에서 비동기로 온다 — 시나리오는 보내기 전 값을 `Store` 하고 바뀔 때까지 기다린다(예전에는 답이 올 틈을 프레임 수로 두었다).
             */
            [[nodiscard]] static bool readObjectHashReports( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                uint32                      reports  = 0;
                for ( int32 processIndex = 0; pSession != nullptr && processIndex < pSession->getProcessCount(); ++processIndex )
                    reports += pSession->getProcessStatus( processIndex )._objectHashReportCount;
                outValue = static_cast<float64>( reports );
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlayConnectedClients` — 지금 서버에 붙어 있다고 알린 클라이언트 수입니다(서버를 잃은 클라이언트는 빠진다). */
            [[nodiscard]] static bool readConnectedClients( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( pSession->getConnectedClientCount() ) : 0.0;
                return true;
            }

            /** @brief `netplay.start [dedicated|listen] [<플레이어 수>] [scene=<리소스 경로>]` — 구성을 고치고 세션을 시작합니다. 씬을 주면 모든 프로세스가 그 씬으로 시작합니다(구성에 남지 않는다). */
            static bool runStart( const vector<string>& listArgument, string& outReply )
            {
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                if ( pSession == nullptr )
                {
                    outReply = "the editor is not running";
                    return true;
                }
                EditorNetPlaySettings& settings = pSession->getSettings();
                settings._mode                  = settings._mode == EditorNetMode::Standalone ? EditorNetMode::DedicatedServer : settings._mode;
                string firstScenePath;
                for ( const string& argument : listArgument )
                {
                    if ( StringUtil::startsWith( argument, kSceneArgumentPrefix ) )
                    {
                        firstScenePath = argument.substr( kSceneArgumentPrefix.size() );
                        if ( firstScenePath.empty() )
                            return false;
                        continue;
                    }
                    int32      playerCount          = 0;
                    const bool bPlayerCountArgument = StringUtil::parseInt( argument, playerCount ) && 1 <= playerCount && playerCount <= EditorNetModeCombo::kMaxPlayerCount;
                    if ( argument == "dedicated" )
                        settings._mode = EditorNetMode::DedicatedServer;
                    else if ( argument == "listen" )
                        settings._mode = EditorNetMode::ListenHost;
                    else if ( bPlayerCountArgument )
                        settings._clientCount = playerCount;
                    else
                        return false;
                }
                const bool bStarted = pSession->start( pSession->getSettings(), firstScenePath );
                outReply            = bStarted ? "multiplayer session started on UDP port " + to_string( pSession->getPort() ) : "multiplayer session did not start (see the log)";
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlayStopping` — `netplay.stop` 뒤 프로세스 거두기 · 트레이스 합치기가 아직이면 1 입니다(시나리오가 `<WaitUntil>` 로 끝을 기다린다). */
            [[nodiscard]] static bool readStopping( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                outValue                             = pSession != nullptr && pSession->isStopping() ? 1.0 : 0.0;
                return true;
            }

            /**
             * @brief `netplay.stop` — 모두에 정상 종료를 알리고 멈춤을 시작합니다(기다리지 않는다).
             * @details 끝은 감시 값 `Editor.NetPlayStopping` == 0 이고, 그때 `Multiplayer session stopped` 줄(합친 트레이스 경로)이 남는다.
             */
            static bool runStop( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                pSession->requestStop();
                outReply = "multiplayer session stopping (done when Editor.NetPlayStopping is 0)";
                return true;
            }

            /** @brief `netplay.wait <클라이언트 수> [<초>]` — 그만큼 붙을 때까지 이 프레임을 붙잡습니다(자동화 시나리오용 — 시나리오는 프레임 단위라 기동 시간을 모른다). */
            static bool runWait( const vector<string>& listArgument, string& outReply )
            {
                int32      clientCount             = 0;
                int32      seconds                 = kDefaultWaitSeconds;
                const bool bInvalidClientArguments = listArgument.empty() || StringUtil::parseInt( listArgument[0], clientCount ) == false ||
                                                     ( listArgument.size() > 1 && StringUtil::parseInt( listArgument[1], seconds ) == false );
                if ( bInvalidClientArguments )
                    return false;
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                pSession->updateUntil( static_cast<int64>( seconds ) * constant::time::kMillisecondsPerSecond, [&]()
                { return pSession->getConnectedClientCount() >= clientCount; } );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "netplay connected clients %# of %#", pSession->getConnectedClientCount(), clientCount );
                outReply = string( replyBuilder.view() );
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
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                const bool            bSent    = pSession != nullptr && pSession->sendServerCommand( command );
                outReply                       = bSent ? "sent to the server: " + command : string( "no server to send to" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorNetPlayProcessCount, "Editor.NetPlayProcessCount", "Processes of the running multiplayer session (server and clients, 0 when stopped)",
                         &MultiplayerSessionViewModelInternal::readProcessCount );
    SW_AUTOMATION_WALL_WATCH( editorNetPlayStopping, "Editor.NetPlayStopping", "1 while a stopped multiplayer session is still reaping its processes and merging traces",
                              &MultiplayerSessionViewModelInternal::readStopping );
    SW_AUTOMATION_WALL_WATCH( editorNetPlayObjectHashReports, "Editor.NetPlayObjectHashReports",
                              "state.objectline replies received from the session's processes so far (remember it, send the command, wait until it differs)",
                              &MultiplayerSessionViewModelInternal::readObjectHashReports );
    SW_AUTOMATION_WATCH( editorNetPlayConnectedClients, "Editor.NetPlayConnectedClients", "Clients of the multiplayer session that logged a connection to the server and have not lost it since",
                         &MultiplayerSessionViewModelInternal::readConnectedClients );
    SW_DEV_COMMAND( NetPlayStart, "netplay.start", "netplay.start [dedicated|listen] [<players>] [scene=<resource path>]", "Start the editor's multi-process play session",
                    &MultiplayerSessionViewModelInternal::runStart );
    SW_DEV_COMMAND( NetPlayStop, "netplay.stop", "netplay.stop", "Stop every process of the multi-process play session and merge their traces",
                    &MultiplayerSessionViewModelInternal::runStop );
    SW_DEV_COMMAND( NetPlayWait, "netplay.wait", "netplay.wait <clients> [<seconds>]", "Block until that many clients of the multiplayer session connected",
                    &MultiplayerSessionViewModelInternal::runWait );
    SW_DEV_COMMAND( NetPlayServer, "netplay.server", "netplay.server <command>", "Send a console line (pause, step N, resume, dev commands) to the session's server",
                    &MultiplayerSessionViewModelInternal::runServer );
} // namespace sw::editor

namespace sw::editor
{
    EditorNetPlaySession* MultiplayerSessionViewModel::findSession()
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        return pContext != nullptr ? &pContext->getSubsystem<EditorNetPlaySession>() : nullptr;
    }

    EditorNetPlaySession& MultiplayerSessionViewModel::getSession()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>();
    }

    void MultiplayerSessionViewModel::gather( MultiplayerSessionViewState& outState ) const
    {
        EditorNetPlaySession& session = getSession();
        const int32           count   = session.getProcessCount();
        outState._listProcess.resize( static_cast<size_t>( count ) );
        for ( int32 processIndex = 0; processIndex < count; ++processIndex )
        {
            const EditorNetPlayProcessStatus status = session.getProcessStatus( processIndex );
            MultiplayerProcessRow&           row    = outState._listProcess[static_cast<size_t>( processIndex )];
            row._label                              = status._label;
            row._lastLine                           = status._lastLine;
            row._emulationReport                    = status._emulationReport;
            row._emulationReportCount               = status._emulationReportCount;
            row._processID                          = status._processID;
            row._exitCode                           = status._exitCode;
            row._bRunning                           = status._bRunning ? SW_TRUE : SW_FALSE;
            row._bConnected                         = status._bConnected ? SW_TRUE : SW_FALSE;
        }
        const EditorNetPlayPresetStore& store = session.getPresetStore();
        outState._listPresetName.resize( store.getPresets().size() );
        for ( size_t index = 0; index < store.getPresets().size(); ++index )
        {
            outState._listPresetName[index] = store.getPresets()[index]._name;
        }
        outState._settings           = session.getSettings();
        outState._selectedPresetName = store.getSelectedPresetName();
        outState._mergedTracePath    = session.getMergedTracePath();
        outState._port               = session.getPort();
        outState._debugChannelPort   = session.getDebugChannelPort();
        outState._bRunning           = session.isRunning() ? SW_TRUE : SW_FALSE;
        outState._bStopping          = session.isStopping() ? SW_TRUE : SW_FALSE;
    }

    void MultiplayerSessionViewModel::gatherLog( int32 processIndex, vector<MultiplayerLogRow>& outListRow ) const
    {
        size_t rowCount = 0;
        for ( const EditorNetPlayLogLine& line : getSession().getLogLines() )
        {
            if ( processIndex >= 0 && line._processIndex != processIndex )
                continue;
            if ( rowCount == outListRow.size() )
                outListRow.emplace_back();
            MultiplayerLogRow& row = outListRow[rowCount++]; // 있는 줄에 덮어써 글자 저장 공간을 다시 쓴다
            row._text              = line._text;
            row._processIndex      = line._processIndex;
        }
        outListRow.resize( rowCount );
    }

    void MultiplayerSessionViewModel::setSettings( const EditorNetPlaySettings& settings )
    {
        getSession().getSettings() = settings;
    }

    void MultiplayerSessionViewModel::selectPreset( const string& name )
    {
        EditorNetPlaySession&     session = getSession();
        EditorNetPlayPresetStore& store   = session.getPresetStore();
        if ( store.selectPreset( name ) )
            session.getSettings() = *store.findSelectedPreset();
    }

    bool MultiplayerSessionViewModel::savePreset( const string& name )
    {
        EditorNetPlaySession& session = getSession();
        return session.getPresetStore().savePreset( name, session.getSettings() );
    }

    void MultiplayerSessionViewModel::removePreset( const string& name )
    {
        (void)getSession().getPresetStore().removePreset( name ); // 실패 이유는 저장소가 오류로 남긴다
    }

    void MultiplayerSessionViewModel::start()
    {
        EditorNetPlaySession& session = getSession();
        (void)session.start( session.getSettings(), string_view{} ); // 실패 이유는 세션이 로그에 남긴다
    }

    void MultiplayerSessionViewModel::requestStop()
    {
        getSession().requestStop();
    }

    bool MultiplayerSessionViewModel::sendServerCommand( string_view command )
    {
        return getSession().sendServerCommand( command );
    }

    bool MultiplayerSessionViewModel::sendProcessCommand( int32 processIndex, string_view command )
    {
        return getSession().sendProcessCommand( processIndex, command );
    }
} // namespace sw::editor
