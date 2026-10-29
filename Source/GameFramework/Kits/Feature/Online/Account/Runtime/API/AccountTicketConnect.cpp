#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountTicketConnect.h"

#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/Clock.h"
#include "Core/Time/MonotonicClock.h"

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
    } // namespace
} // namespace sw

namespace sw
{
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
} // namespace sw
