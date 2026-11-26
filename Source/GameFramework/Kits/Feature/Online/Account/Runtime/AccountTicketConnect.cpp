#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountTicketConnect.h"

#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/Clock.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"

namespace sw
{
    namespace
    {
        struct AccountTicketConnectInternal
        {
            /** @brief 전송을 한 번 돌리고 쉬는 간격입니다(전송 스레드 없이 `update` 가 돌린다). */
            static constexpr int64 kPollIntervalMilliseconds = 5;

            static string describeFailure( const AccountClientReply& reply )
            {
                StringBuilder<constant::buffer::kMax256> resultBuilder;
                resultBuilder.appendFormat( "error %#, result %#", static_cast<int32>( reply._errorCode ), static_cast<int32>( reply._result._result ) );
                return string( resultBuilder.view() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_MODULE_SUBSYSTEM( Engine, Process, AccountTicketConnector, AccountTicketConnector );

    AccountTicketConnector::AccountTicketConnector()
        : _tlsContext{}
        , _pTransport{}
        , _client{}
        , _account{}
        , _loginName{}
        , _password{}
        , _serverID{}
        , _deadline{ 0 }
        , _bServiceRegistered{ SW_FALSE }
        , _bPending{ SW_FALSE }
        , _bLoginSent{ SW_FALSE }
        , _bTicketSent{ SW_FALSE }
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
        StringUtil::wipeSecret( _password );
        _bPending = SW_FALSE;
    }

    unexpected<Error> AccountTicketConnector::fail( Error error )
    {
        shutdown();
        return unexpected( std::move( error ) );
    }

    expected<NetAddress> AccountTicketConnector::validateRequest( const AccountTicketRequest& request )
    {
        const bool bComplete = request._loginAddress.empty() == false && request._loginName.empty() == false && request._password.empty() == false &&
                               request._serverID.empty() == false && request._timeoutMilliseconds > 0;
        if ( bComplete == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a ticket request needs the login address, account name, password, server id and a positive timeout" } );
        NetAddress loginAddress;
        if ( NetAddress::parse( request._loginAddress, 0, loginAddress ) == false || loginAddress.isValid() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the login address needs host:port", request._loginAddress } );
        return loginAddress;
    }

    expected<void> AccountTicketConnector::acquire( AccountTicketRequest request, NetConnectCredentials& outCredentials )
    {
        shutdown();
        // 전송 · TLS 를 만들기 전에 거절한다 — 빈 요청은 연결도 보안 공급자도 건드리지 않는다.
        if ( const expected<NetAddress> checked = validateRequest( request ); checked.has_value() == false )
            return unexpected( checked.error() );
        _pTransport = StreamTransportFactory::createPlatformTransport();
        if ( _pTransport == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "this platform has no stream transport" } );
        // 서버의 서비스 스트림은 늘 TLS 1.3 이다 — 평문으로 붙으면 핸드셰이크가 닫힌다.
        expected<unique_ptr<ITLSContext>> tlsContext = NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), request._tlsTrustFile, "" );
        if ( tlsContext.has_value() == false )
            return fail( tlsContext.error() );
        _tlsContext = std::move( *tlsContext );
        // `begin` 은 이전 연결만 닫고 위에서 만든 전송 · TLS 는 남긴다.
        const expected<void> begun = begin( std::move( request ), *_pTransport, OnlineClientSecurity::tls( *_tlsContext ), SystemMonotonicClock{}.nowMilliseconds() );
        if ( begun.has_value() == false )
            return fail( begun.error() );
        for ( ;; )
        {
            const expected<AccountTicketStep> step = poll( SystemMonotonicClock{}.nowMilliseconds(), outCredentials );
            if ( step.has_value() == false )
                return unexpected( step.error() );
            if ( *step == AccountTicketStep::Received )
                return {}; // 연결은 열어 둔다 — 티켓은 이 계정 세션에 묶여 있다
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + AccountTicketConnectInternal::kPollIntervalMilliseconds * constant::time::kNanosecondsPerMillisecond );
        }
    }

    expected<void> AccountTicketConnector::begin( AccountTicketRequest request, IStreamTransport& transport, OnlineClientSecurity security, MonotonicMilliseconds nowMs )
    {
        _client.shutdown(); // 이전 연결만 닫는다 — 부르는 쪽의 전송 · TLS(`acquire` 가 막 만든 것)는 그대로 쓴다
        _bPending                               = SW_FALSE;
        const expected<NetAddress> loginAddress = validateRequest( request );
        if ( loginAddress.has_value() == false )
            return unexpected( loginAddress.error() );
        if ( _bServiceRegistered == false )
        {
            AccountClientInfo info;
            info._build    = request._gameBuild;
            info._platform = request._platform;
            _account.initialize( &_client, info );
            if ( _client.registerClientService( &_account ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the account client could not be registered (method range already taken)" } );
            _bServiceRegistered = SW_TRUE;
        }
        OnlineServiceClientConfig settings{ security, OnlineRetryPolicy::none() }; // 막는 개발 도구다 — 시한 안의 실패를 바로 보인다
        settings._transportSettings._ioThreadCount = 0;                            // `poll` 의 `update` 가 전송을 돌린다
        settings._serverAddress                    = *loginAddress;
        settings._gameBuild                        = request._gameBuild;
        if ( expected<void> result = _client.initialize( &transport, settings ); result.has_value() == false )
            return result;
        _loginName   = std::move( request._loginName );
        _password    = std::move( request._password );
        _serverID    = std::move( request._serverID );
        _deadline    = nowMs + request._timeoutMilliseconds;
        _bPending    = SW_TRUE;
        _bLoginSent  = SW_FALSE;
        _bTicketSent = SW_FALSE;
        return {};
    }

    expected<AccountTicketStep> AccountTicketConnector::poll( MonotonicMilliseconds nowMs, NetConnectCredentials& outCredentials )
    {
        if ( _bPending == SW_FALSE )
            return unexpected( Error{ ErrorKind::InvalidArgument, "no ticket request in progress - call begin first" } );
        if ( nowMs >= _deadline )
        {
            return fail( Error{ ErrorKind::ExternalFailure, _bLoginSent ? ( _bTicketSent ? "timed out waiting for the game ticket" : "timed out waiting for the login reply" )
                                                                        : "timed out connecting to the login server" } );
        }
        _client.update( nowMs );
        const OnlineClientState state = _client.getState();
        if ( state == OnlineClientState::VersionMismatch || state == OnlineClientState::HandshakeFailed )
            return fail( Error{ ErrorKind::InvalidArgument, "the login server refused the handshake (version mismatch or bad Hello)" } );
        if ( _client.isReady() && _bLoginSent == SW_FALSE )
        {
            _bLoginSent = SW_TRUE;
            (void)_account.login( _loginName, _password );
            StringUtil::wipeSecret( _password ); // 보낸 뒤에는 쓰지 않는다 — 크래시 덤프 · 힙에 남기지 않는다
        }
        vector<AccountClientReply> listReply;
        (void)_account.pollReplies( listReply );
        for ( const AccountClientReply& reply : listReply )
        {
            if ( reply._operation == AccountMethodName::Login )
            {
                if ( reply._errorCode != 0 || reply._result != LoginResult::Ok )
                    return fail( Error{ ErrorKind::ExternalFailure, "login refused", AccountTicketConnectInternal::describeFailure( reply ) } );
                if ( _bTicketSent == SW_FALSE )
                {
                    _bTicketSent = SW_TRUE;
                    (void)_account.issueGameTicket( _serverID );
                }
            }
            else if ( reply._operation == AccountMethodName::IssueGameTicket )
            {
                if ( reply._errorCode != 0 || reply._result != LoginResult::Ok )
                    return fail( Error{ ErrorKind::ExternalFailure, "game ticket refused", AccountTicketConnectInternal::describeFailure( reply ) } );
                // 티켓 시한(`_expiresAtMs`)은 여기서 보지 않는다 — 시한은 봉인된 토큰 안에 있어 게임 서버(LoginTicketAuthority)가 판정하고, 클라이언트 벽시계로 보면 시계가 어긋난 기기만 막는다(EOS · Steam 세션 티켓도 서버가 본다).
                AccountClient::makeConnectCredentials( reply._ticket, outCredentials );
                _bPending = SW_FALSE;
                SW_LOG_INFO_ID( "AccountTicketConnect.GameTicketServerReceived", "Game ticket for server '%#' received (the account session stays open)", _serverID.c_str() );
                return AccountTicketStep::Received;
            }
        }
        return AccountTicketStep::Pending;
    }
} // namespace sw
