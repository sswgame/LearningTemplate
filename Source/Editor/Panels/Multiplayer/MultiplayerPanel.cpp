#include "pch.h"

#include "Editor/Panels/Multiplayer/MultiplayerPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorNetModeCombo.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "MultiplayerPanel" );

    namespace
    {
        struct MultiplayerPanelInternal
        {
            /** @brief `netplay.wait` 의 기본 시한(초)입니다. */
            static constexpr int32 kDefaultWaitSeconds = 60;

            static EditorNetPlaySession* findSession()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? &pContext->getSubsystem<EditorNetPlaySession>() : nullptr;
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
                    else if ( StringUtil::parseInt( argument, playerCount ) && 1 <= playerCount && playerCount <= EditorNetModeCombo::kMaxPlayerCount )
                        settings._clientCount = playerCount;
                    else
                        return false;
                }
                const bool bStarted = pSession->start( pSession->getSettings() );
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
                pSession->updateUntil( static_cast<int64>( seconds ) * 1000, [&]()
                { return pSession->getConnectedClientCount() >= clientCount; } );
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
        , _listTab{}
        , _tabGeneration{ invalid_index::kUint32 }
        , _targetProcessIndex{ EditorNetPlaySession::kServerProcessIndex }
    {
    }

    void MultiplayerPanel::syncTabs()
    {
        using Registry                              = EditorExtensionRegistry<EditorMultiplayerTabRegistration>;
        const EditorExtensionRegistrationList& list = Registry::getList();
        if ( list.getGeneration() == _tabGeneration )
            return;
        _tabGeneration = list.getGeneration();
        _listTab.clear();
        _listTab.reserve( Registry::getCount() );
        for ( uint32 index = 0; index < Registry::getCount(); ++index )
        {
            const EditorMultiplayerTabRegistration& registration = Registry::getAt( index );
            TabEntry&                               entry        = _listTab.emplace_back();
            entry._pRegistration                                 = &registration;
            entry._pTab                                          = registration._pCreate();
        }
    }

    int32 MultiplayerPanel::drawTargetPicker( EditorNetPlaySession& session )
    {
        const int32 processCount = session.getProcessCount();
        if ( processCount == 0 )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "NoTarget" ) );
            return -1;
        }
        _targetProcessIndex = MathUtil::clamp( _targetProcessIndex, 0, processCount - 1 );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( "Target" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 10.0f );
        const EditorNetPlayProcessStatus target = session.getProcessStatus( _targetProcessIndex );
        if ( ImGui::BeginCombo( "##MultiplayerTarget", target._label.c_str() ) )
        {
            for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
            {
                if ( ImGui::Selectable( session.getProcessStatus( processIndex )._label.c_str(), processIndex == _targetProcessIndex ) )
                    _targetProcessIndex = processIndex;
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "Target" ) );
        return _targetProcessIndex;
    }

    void MultiplayerPanel::drawContent()
    {
        EditorNetPlaySession* pSession = MultiplayerPanelInternal::findSession();
        if ( pSession == nullptr )
            return;
        syncTabs();
        MultiplayerTabContext context;
        context._pSession           = pSession;
        context._targetProcessIndex = drawTargetPicker( *pSession );
        if ( ImGui::BeginTabBar( "##MultiplayerTabs" ) )
        {
            for ( TabEntry& entry : _listTab )
            {
                if ( ImGui::BeginTabItem( entry._pRegistration->_pLabel ) )
                {
                    entry._pTab->drawTab( context );
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    SW_EDITOR_PANEL( MultiplayerPanel, "multiplayer", EditorPanelCategory::Tool, 2035 );
} // namespace sw::editor
