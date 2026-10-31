#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountTicketConnect.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/Clock.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Net/GameNetSession.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw
{
    SW_LOG_CALLER( "AccountTicketConnect" );

    namespace
    {
        struct AccountTicketConnectInternal
        {
            /** @brief 전송을 한 번 돌리고 쉬는 간격입니다(전송 스레드 없이 `update` 가 돌린다). */
            static constexpr int64 kPollIntervalMilliseconds = 5;

            static string describeFailure( const AccountClientReply& reply )
            {
                return formatToString( "error %#, result %#", static_cast<int32>( reply._errorCode ), static_cast<int32>( reply._result ) );
            }
        };

        /**
         * @brief 개발 명령 `net.ticketconnect` — 로그인 서버에 로그인해 접속 티켓을 받고, 그 티켓으로 게임 서버에 붙는 클라이언트 세션을 엽니다(OnlineKits F5 두 프로세스 시나리오).
         * @details `-gv_standardInputCommands=1` 로 띄운 App 의 표준 입력으로 보내면 시나리오 구동기가 계정 로그인 → 티켓 → 암호화 UDP 접속을 한 줄로 한다.
         *          막는 호출이라 시한(10 초)까지 게임 스레드가 선다. 세션이 이미 열려 있으면 거절한다(자격 없는 세션으로 대신하지 않는다).
         */
        struct AccountTicketConnectCommandInternal
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
                if ( NetAddress::parse( listArgument[4], 0, gameAddress ) == false || gameAddress.isValid() == false )
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
#else
                request._platform = "linux";
#endif
                request._timeoutMilliseconds = kTimeoutMilliseconds;
                NetConnectCredentials credentials;
                string                error;
                // 티켓이 계정 세션에 묶여 있어 로그인 연결을 쥔 도우미는 키트의 모듈 서브시스템이다(엔진 범위 — 명령은 한 번에 하나만 성공한다).
                AccountTicketConnector& connector = game::getSubsystem<AccountTicketConnector>();
                if ( connector.acquire( std::move( request ), credentials, error ) == false )
                {
                    outReply = "ticketconnect: " + error;
                    return true;
                }
                GameNetSessionParams params;
                if ( pSession->makeTicketClientParams( gameAddress, credentials, params ) == false || pSession->initialize( params ) == false )
                {
                    outReply = "ticketconnect: the ticket client session did not open (reason in the log)";
                    connector.shutdown();
                    return true;
                }
                outReply = "ticketconnect: connecting to " + listArgument[4] + " with the account ticket";
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_MODULE_SUBSYSTEM( Engine, AccountTicketConnector, AccountTicketConnector );

    AccountTicketConnector::AccountTicketConnector()
        : _tlsContext{}
        , _pTransport{}
        , _client{}
        , _account{}
        , _bServiceRegistered{ false }
    {
    }

    AccountTicketConnector::~AccountTicketConnector()
    {
        shutdown();
    }

    void AccountTicketConnector::shutdown()
    {
        _client.shutdown();
        _pTransport.reset();
        _tlsContext.reset();
    }

    bool AccountTicketConnector::acquire( AccountTicketRequest request, NetConnectCredentials& outCredentials, string& outError )
    {
        shutdown();
        const bool bComplete = request._loginAddress.empty() == false && request._loginName.empty() == false && request._password.empty() == false &&
                               request._serverID.empty() == false && request._timeoutMilliseconds > 0;
        if ( bComplete == false )
        {
            outError = "a ticket request needs the login address, account name, password, server id and a positive timeout";
            return false;
        }
        NetAddress loginAddress;
        if ( NetAddress::parse( request._loginAddress, 0, loginAddress ) == false || loginAddress.isValid() == false )
        {
            outError = "login address '" + request._loginAddress + "' needs host:port";
            return false;
        }
        _pTransport = StreamTransportFactory::createPlatformTransport();
        if ( _pTransport == nullptr )
        {
            outError = "this platform has no stream transport";
            return false;
        }
        if ( _bServiceRegistered == false )
        {
            AccountClientInfo info;
            info._build    = request._gameBuild;
            info._platform = request._platform;
            _account.initialize( &_client, info );
            if ( _client.registerClientService( &_account ) == false )
            {
                outError = "the account client could not be registered (method range already taken)";
                return false;
            }
            _bServiceRegistered = true;
        }
        // 서버의 서비스 스트림은 늘 TLS 1.3 이다 — 평문으로 붙으면 핸드셰이크가 닫힌다.
        _tlsContext = NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), request._tlsTrustFile, "", outError );
        if ( _tlsContext == nullptr )
            return false;
        OnlineServiceClientConfig settings{ OnlineClientSecurity::tls( *_tlsContext ) };
        settings._transportSettings._ioThreadCount = 0; // 이 함수의 `update` 가 전송을 돌린다
        settings._serverAddress                    = loginAddress;
        settings._gameBuild                        = request._gameBuild;
        if ( _client.initialize( _pTransport.get(), settings, outError ) == false )
            return false;

        const Deadline deadline    = Deadline::afterMilliseconds( request._timeoutMilliseconds );
        bool           bLoginSent  = false;
        bool           bTicketSent = false;
        while ( deadline.isExpired() == false )
        {
            _client.update( SystemMonotonicClock{}.nowMilliseconds() );
            const OnlineClientState state = _client.getState();
            if ( state == OnlineClientState::VersionMismatch || state == OnlineClientState::HandshakeFailed )
            {
                outError = "the login server refused the handshake (version mismatch or bad Hello)";
                shutdown();
                return false;
            }
            if ( _client.isReady() && bLoginSent == false )
            {
                bLoginSent = true;
                (void)_account.login( request._loginName, request._password );
                request._password.clear(); // 보낸 뒤에는 쓰지 않는다
            }
            vector<AccountClientReply> listReply;
            (void)_account.pollReplies( listReply );
            for ( const AccountClientReply& reply : listReply )
            {
                if ( reply._operation == AccountClientOperation::Login )
                {
                    if ( reply._errorCode != 0 || reply._result != LoginResult::Ok )
                    {
                        outError = "login refused: " + AccountTicketConnectInternal::describeFailure( reply );
                        shutdown();
                        return false;
                    }
                    if ( bTicketSent == false )
                    {
                        bTicketSent = true;
                        (void)_account.issueGameTicket( request._serverID );
                    }
                }
                else if ( reply._operation == AccountClientOperation::IssueGameTicket )
                {
                    if ( reply._errorCode != 0 || reply._result != LoginResult::Ok )
                    {
                        outError = "game ticket refused: " + AccountTicketConnectInternal::describeFailure( reply );
                        shutdown();
                        return false;
                    }
                    AccountClient::makeConnectCredentials( reply._ticket, outCredentials );
                    SW_LOG_INFO( "Game ticket for server '%#' received (the account session stays open)", request._serverID.c_str() );
                    return true; // 연결은 열어 둔다 — 티켓은 이 계정 세션에 묶여 있다
                }
            }
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + AccountTicketConnectInternal::kPollIntervalMilliseconds * constant::kNanosecondsPerMillisecond );
        }
        outError = bLoginSent ? ( bTicketSent ? "timed out waiting for the game ticket" : "timed out waiting for the login reply" ) : "timed out connecting to the login server";
        shutdown();
        return false;
    }

    SW_DEV_COMMAND( NetTicketConnect, "net.ticketconnect", "net.ticketconnect <loginHost:port> <account> <password> <serverID> <gameHost:port> [build [trustFile]]",
                    "Log in to the login service, get a game connect ticket and open the encrypted client session with it", &AccountTicketConnectCommandInternal::run );
} // namespace sw
