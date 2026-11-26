/**
 * @file AccountTicketConnectorDevCommands.cpp
 * @brief `AccountTicketConnector` 의 개발 명령입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/Network/NetAddress.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/Clock.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountTicketConnector.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"

namespace sw
{
    namespace
    {
        struct AccountTicketConnectorCommandDevInternal
        {
            static constexpr int64 kTimeoutMilliseconds = 10000;

            static bool run( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 5 || listArgument.size() > 7 )
                    return false;
                GameNetSession* pSession = game::findSubsystem<GameNetSession>();
                if ( pSession == nullptr )
                {
                    outReply = "ticketconnect: no game net session service";
                    return true;
                }
                if ( pSession->isActive() )
                {
                    outReply = "ticketconnect: the net session is already open - start without -gv_netConnectAddress / -gv_netListenPort";
                    return true;
                }
                NetAddress gameAddress;
                // 이름도 받는다(개발 명령이라 막는 해석이어도 된다) — 웹의 wss 는 이름으로 인증서를 확인한다.
                if ( PlatformSocketUtil::resolveAddress( listArgument[4], 0, gameAddress ) == false || gameAddress.isValid() == false )
                {
                    outReply = "ticketconnect: the game address needs host:port";
                    return true;
                }
                AccountTicketRequest request;
                request._loginAddress = listArgument[0];
                request._loginName    = listArgument[1];
                request._password     = listArgument[2];
                request._serverID     = listArgument[3];
                request._gameBuild    = listArgument.size() >= 6 ? listArgument[5] : "1.0.0";
                request._tlsTrustFile = listArgument.size() == 7 ? listArgument[6] : "";
#if defined( SW_PLATFORM_WINDOWS )
                request._platform = "windows";
#elif defined( SW_PLATFORM_LINUX )
                request._platform = "linux";
#elif defined( SW_PLATFORM_ANDROID )
                request._platform = "android";
#elif defined( SW_PLATFORM_WEB )
                request._platform = "web";
#else
    #error "Unsupported platform"
#endif
                request._timeoutMilliseconds = kTimeoutMilliseconds;
                NetConnectCredentials credentials;
                // 티켓이 계정 세션에 묶여 있어 로그인 연결을 쥔 도우미는 키트의 모듈 서브시스템이다(엔진 범위 — 명령은 한 번에 하나만 성공한다).
                AccountTicketConnector& connector = game::getSubsystem<AccountTicketConnector>();
                if ( const expected<void> acquired = connector.acquire( std::move( request ), credentials ); acquired.has_value() == false )
                {
                    StringBuilder<constant::buffer::kMax256> reply;
                    reply.appendFormat( "ticketconnect: %# (%#)", acquired.error().message(), acquired.error().detail() );
                    outReply = string( reply.view() );
                    return true;
                }
                GameNetSessionParams params;
                if ( pSession->makeTicketClientParams( gameAddress, credentials, params ) == false || pSession->initialize( params ) == false )
                {
                    outReply = "ticketconnect: the ticket client session did not open (reason in the log)";
                    connector.shutdown();
                    return true;
                }
                // 기동 때 연 세션처럼(`GameInstance::initialize`) 씬 매니저에 넷 링크를 건다 — 걸지 않으면 씬의 네트워크 컴포넌트가 늦게 연 세션을 찾지 못한다.
                SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
                if ( pSceneManager != nullptr )
                    pSceneManager->setNetLink( pSession->makeNetLink() );
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "ticketconnect: connecting to %# with the account ticket", listArgument[4] );
                outReply = string( reply.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( NetTicketConnect, "net.ticketconnect", "net.ticketconnect <loginHost:port> <account> <password> <serverID> <gameHost:port> [build [trustFile]]",
                    "Log in to the login service, get a game connect ticket and open the encrypted client session with it", &AccountTicketConnectorCommandDevInternal::run );
} // namespace sw
