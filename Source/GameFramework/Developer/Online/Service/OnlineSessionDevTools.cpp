/**
 * @file OnlineSessionDevTools.cpp
 * @brief `OnlineSession` 의 개발 명령 · 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Service/IOnlineLocalServer.h"
#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

namespace sw
{
    namespace
    {
        struct OnlineSessionDevInternal
        {
            // 개발 명령 - 게임 코드 없이 세션을 시나리오와 스크립트가 움직인다.
            static bool runConnect( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                {
                    outReply = "online: no game instance";
                    return true;
                }
                const expected<void> connected = pSession->connect();
                if ( connected.has_value() )
                {
                    outReply = "online: connecting";
                    return true;
                }
                StringBuilder<constant::buffer::kMax512> reply;
                reply.appendFormat( "online: %# (%#)", connected.error().message(), connected.error().detail() );
                outReply = string( reply.view() );
                return true;
            }

            static bool runDisconnect( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession != nullptr )
                    pSession->disconnect();
                outReply = "online: disconnected";
                return true;
            }

            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                {
                    outReply = "online: no game instance";
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "online: %#, %# kit clients, local server %#", getEnumName( pSession->getState() ), pSession->getClientCount(), pSession->hasLocalServer() ? "on" : "off" );
                outReply = string( reply.view() );
                return true;
            }

            static bool runLocalStart( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                {
                    outReply = "online: no game instance";
                    return true;
                }
                if ( const expected<void> started = pSession->startLocalServer( listArgument[0] ); started.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "OnlineSession.LocalServerFailed", "The local online server did not start: %# (%#)", started.error().message(), started.error().detail() );
                    StringBuilder<constant::buffer::kMax512> reply;
                    reply.appendFormat( "online: %# (%#)", started.error().message(), started.error().detail() );
                    outReply = string( reply.view() );
                    return true;
                }
                outReply = "online: local server up";
                return true;
            }

            [[nodiscard]] static bool readState( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getState() );
                return true;
            }

            [[nodiscard]] static bool readClientCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getClientCount() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( OnlineConnect, "online.connect", "online.connect", "Connect the online session to the server in gamesettings", &OnlineSessionDevInternal::runConnect );
    SW_DEV_COMMAND( OnlineDisconnect, "online.disconnect", "online.disconnect", "Close the online session and stop its in-process server", &OnlineSessionDevInternal::runDisconnect );
    SW_DEV_COMMAND( OnlineStatus, "online.status", "online.status", "Show the online session state, its kit client count and whether an in-process server runs", &OnlineSessionDevInternal::runStatus );
    SW_DEV_COMMAND( OnlineLocalStart, "online.local.start", "online.local.start <server config path>", "Start an in-process online server from that server config and connect the session to it (development only)", &OnlineSessionDevInternal::runLocalStart );
    SW_AUTOMATION_WATCH( onlineSessionState, "Online.SessionState", "Online session state: 0 disconnected, 1 connecting, 2 negotiating, 3 ready, 4 version mismatch, 5 handshake failed", &OnlineSessionDevInternal::readState );
    SW_AUTOMATION_WATCH( onlineSessionClientCount, "Online.SessionClientCount", "Kit clients the online session built from the loaded kits", &OnlineSessionDevInternal::readClientCount );
} // namespace sw
