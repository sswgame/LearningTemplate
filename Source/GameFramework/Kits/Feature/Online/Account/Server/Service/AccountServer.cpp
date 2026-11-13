#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"

namespace sw
{
    namespace
    {
        struct AccountServerInternal
        {
            static bool isGrantOperation( LoginOperation operation )
            {
                return operation == LoginOperation::Login || operation == LoginOperation::GuestLogin || operation == LoginOperation::PlatformLogin ||
                       operation == LoginOperation::Resume;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AccountServer::AccountServer()
        : _mapCallToPending{}
        , _mapAccountToSession{}
        , _mapAccountToReasonCode{}
        , _uniqueSetLoginConnection{}
        , _listCompletionScratch{}
        , _listEventScratch{}
        , _listBusSubscription{}
        , _presence{}
        , _sessionControl{ *this }
        , _settings{}
        , _pLoginService{ nullptr }
        , _nowMs{ 0 }
        , _nextPurgeMs{ 0 }
        , _nextRefreshMs{ 0 }
        , _bHostAttached{ SW_FALSE }
    {
        setReady( false );
    }

    void AccountServer::initialize( LoginService* pLoginService, const AccountServerConfig& settings )
    {
        _pLoginService = pLoginService;
        _settings      = settings;
        _presence.setSettings( settings._presence );
        initializeCalls( settings._pMetricRegistry, "account" );
        setReady( pLoginService != nullptr );
    }

    void AccountServer::shutdown()
    {
        _mapCallToPending.clear();
        _calls.clear();
        setReady( false );
        _mapAccountToSession.clear();
        _mapAccountToReasonCode.clear();
        _uniqueSetLoginConnection.clear();
        _listBusSubscription.clear();
        _presence.shutdown();
        _pLoginService = nullptr;
    }

    void AccountServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _listBusSubscription.clear(); // attachHost 가 건 구독을 같은 짝에서 푼다(다른 키트 서버와 같이)
        _presence.detach();           // 캐시 응답은 호스트가 이미 Unavailable 로 회수했다
        _bHostAttached = SW_FALSE;
    }

    uint16 AccountServer::checkRequest( OnlineServiceHost& host, const OnlineCallContext& context )
    {
        (void)host;
        _nowMs = context._nowMs;
        return OnlineError::kOk;
    }

    template <typename TMethod>
    bool AccountServer::beginLogin( OnlineServerCall<TMethod> call, const OnlineCallContext& context )
    {
        // 한 연결에는 계정 하나만 쓴다. 겹쳐 보낸 로그인은 둘째 완료에서 묶지 못한 채 Ok 를 답해 응답의 세션과 연결의 주체가 갈린다.
        if ( context._accountID != kInvalidAccountID || _uniqueSetLoginConnection.count( context._connection.packed() ) != 0 )
        {
            (void)fail( call, OnlineError::kInvalidRequest );
            return false;
        }
        _mapCallToPending[call._callID] = PendingCall{ context._connection, SW_TRUE };
        _uniqueSetLoginConnection.insert( context._connection.packed() );
        return true;
    }

    template <typename TMethod>
    const LoginSessionToken* AccountServer::beginSessionCall( OnlineServerCall<TMethod> call, const OnlineCallContext& context )
    {
        const auto sessionIt = _mapAccountToSession.find( context._accountID );
        if ( sessionIt == _mapAccountToSession.end() )
        {
            (void)fail( call, OnlineError::kUnauthenticated );
            return nullptr;
        }
        _mapCallToPending[call._callID] = PendingCall{ context._connection, SW_FALSE };
        return &sessionIt->second._token;
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::Register> call, const OnlineCallContext& context, const AccountCredentialRequest& request )
    {
        _mapCallToPending[call._callID] = PendingCall{ context._connection, SW_FALSE };
        _pLoginService->registerAccount( LoginCredential{ request._loginName, request._password }, context._remoteKey, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::Login> call, const OnlineCallContext& context, const AccountLoginRequest& request )
    {
        if ( beginLogin( call, context ) )
            _pLoginService->login( LoginCredential{ request._loginName, request._password }, request._clientInfo, context._remoteKey, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::GuestLogin> call, const OnlineCallContext& context, const AccountGuestLoginRequest& request )
    {
        if ( beginLogin( call, context ) )
            _pLoginService->guestLogin( request._arrDeviceSecret, request._clientInfo, context._remoteKey, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::PlatformLogin> call, const OnlineCallContext& context, const AccountPlatformRequest& request )
    {
        if ( beginLogin( call, context ) )
            _pLoginService->platformLogin( request._provider, request._ticketBytes, request._clientInfo, context._remoteKey, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::Resume> call, const OnlineCallContext& context, const AccountResumeRequest& request )
    {
        if ( beginLogin( call, context ) )
            _pLoginService->resumeSession( request._token, request._clientInfo, context._remoteKey, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::Logout> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        if ( const LoginSessionToken* pToken = beginSessionCall( call, context ); pToken != nullptr )
            _pLoginService->logout( *pToken, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::LinkCredential> call, const OnlineCallContext& context, const AccountCredentialRequest& request )
    {
        if ( const LoginSessionToken* pToken = beginSessionCall( call, context ); pToken != nullptr )
            _pLoginService->linkCredential( *pToken, LoginCredential{ request._loginName, request._password }, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::LinkPlatform> call, const OnlineCallContext& context, const AccountPlatformRequest& request )
    {
        if ( const LoginSessionToken* pToken = beginSessionCall( call, context ); pToken != nullptr )
            _pLoginService->linkPlatform( *pToken, request._provider, request._ticketBytes, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::IssueGameTicket> call, const OnlineCallContext& context, const AccountGameTicketRequest& request )
    {
        if ( request._serverID.empty() )
        {
            (void)fail( call, OnlineError::kInvalidRequest ); // 서버 id 가 없으면 발급할 곳이 없다
            return;
        }
        if ( const LoginSessionToken* pToken = beginSessionCall( call, context ); pToken != nullptr )
            _pLoginService->issueGameTicket( *pToken, hashed_string( request._serverID ), context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::UnlinkPlatform> call, const OnlineCallContext& context, const AccountProviderRequest& request )
    {
        if ( const LoginSessionToken* pToken = beginSessionCall( call, context ); pToken != nullptr )
            _pLoginService->unlinkPlatform( *pToken, request._provider, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::ListLinks> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        if ( const LoginSessionToken* pToken = beginSessionCall( call, context ); pToken != nullptr )
            _pLoginService->listLinks( *pToken, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::RequestDeletion> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        if ( const LoginSessionToken* pToken = beginSessionCall( call, context ); pToken != nullptr )
            _pLoginService->requestDeletion( *pToken, context._nowMs, call._callID );
    }

    void AccountServer::handleCall( OnlineServerCall<AccountMethod::CancelDeletion> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        if ( const LoginSessionToken* pToken = beginSessionCall( call, context ); pToken != nullptr )
            _pLoginService->cancelDeletion( *pToken, context._nowMs, call._callID );
    }

    void AccountServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        _nowMs = nowMs;
        if ( _bHostAttached == SW_FALSE )
            attachHost( host );
        _calls.publishPendingCount();
        _pLoginService->update( nowMs );
        _listCompletionScratch.clear();
        _pLoginService->takeCompletions( _listCompletionScratch );
        for ( const LoginCompletion& completion : _listCompletionScratch )
        {
            handleCompletion( host, completion );
        }
        // 이벤트는 완료 뒤에 처리한다. 로그인 응답과 탈퇴 응답을 먼저 보내고, 밀려난 세션은 그 뒤에 끊는다.
        _listEventScratch.clear();
        _pLoginService->pollEvents( _listEventScratch );
        for ( const LoginEvent& event : _listEventScratch )
        {
            handleEvent( host, event );
        }
        publishRemoteRevocations( host );
        _presence.update( nowMs );
        if ( nowMs >= _nextPurgeMs )
        {
            _nextPurgeMs = nowMs + _settings._purgeIntervalMs;
            _pLoginService->purgeDueDeletions( nowMs, _settings._purgeCount );
        }
        if ( nowMs >= _nextRefreshMs )
        {
            _nextRefreshMs = nowMs + _settings._refreshIntervalMs;
            _pLoginService->refreshOnlineSessions( nowMs, _settings._refreshCount );
        }
    }

    void AccountServer::onAccountLeft( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        _presence.recordOffline( accountID );
        const auto sessionIt = _mapAccountToSession.find( accountID );
        if ( sessionIt == _mapAccountToSession.end() )
            return; // 이 객체가 끊은 연결(밀려남, 로그아웃)이다. 세션 테이블에서 먼저 뺐다
        const uint64 sessionID = sessionIt->second._token._sessionID;
        _mapAccountToSession.erase( sessionIt );
        _pLoginService->markDisconnected( sessionID, _nowMs ); // 클라이언트가 끊겼으므로 재접속 유예를 시작한다
    }

    void AccountServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        if ( AccountBus::kRevokeTopic.matches( message ) == false )
        {
            (void)_presence.handlePushMessage( message );
            return;
        }
        if ( host.getServerBus() != nullptr && message.getOriginServerID() == host.getServerBus()->getServerID() )
            return; // 이 서버가 보낸 알림이므로 이미 끊었다
        AccountRevoked notification;
        if ( AccountBus::kRevokeTopic.read( message, notification ) != ServerBusReadResult::Read )
            return; // 읽기가 (타입, 결과, 버전)마다 한 번 오류 로그와 카운터를 남겼다 — 그 세션은 영속 레코드 다시 읽기로 끊긴다
        if ( notification._reasonCode.empty() == false && findSessionID( notification._accountID ) == notification._sessionID )
            _mapAccountToReasonCode[notification._accountID] = notification._reasonCode;
        _pLoginService->recordRevokedElsewhere( notification._accountID, notification._sessionID, notification._reason );
    }

    void AccountServer::revokeAccountSessions( AccountID accountID, string_view reasonCode, UnixMilliseconds nowMs )
    {
        _mapAccountToReasonCode[accountID] = string( reasonCode );
        _pLoginService->revokeAccountSessions( accountID, LoginRevokeReason::Administrative, nowMs, 0 ); // 짝이 되는 호출이 없다(태그 0). 완료는 버리고 이벤트로 끊는다
    }

    uint64 AccountServer::findSessionID( AccountID accountID ) const
    {
        const auto sessionIt = _mapAccountToSession.find( accountID );
        return sessionIt == _mapAccountToSession.end() ? 0 : sessionIt->second._token._sessionID;
    }

    void AccountServer::handleCompletion( OnlineServiceHost& host, const LoginCompletion& completion )
    {
        const uint64 callID = completion._requestTag;
        const auto   callIt = _mapCallToPending.find( callID );
        if ( callIt == _mapCallToPending.end() )
            return;
        const PendingCall call = callIt->second;
        _mapCallToPending.erase( callIt );
        if ( call._bLogin == SW_TRUE )
            _uniqueSetLoginConnection.erase( call._connection.packed() );
        if ( respondCommonError( callID, completion._result, completion._grant ) )
            return;

        BitWriter body;
        bool      bWritten = false;
        switch ( completion._operation )
        {
            case LoginOperation::IssueGameTicket:
            {
                bWritten = AccountProtocol::MessageTypes::write( body, AccountTicketReply{ completion._ticket, completion._result._result } );
                break;
            }
            case LoginOperation::ListLinks:
            {
                bWritten = AccountProtocol::MessageTypes::write( body, AccountLinkReply{ completion._linkSummary, completion._result._result } );
                break;
            }
            case LoginOperation::Register:
            case LoginOperation::LinkCredential:
            case LoginOperation::LinkPlatform:
            {
                AccountGrantReply reply{ completion._grant, completion._result._result };
                reply._grant._identity = completion._identity;
                bWritten               = AccountProtocol::MessageTypes::write( body, reply );
                break;
            }
            default:
            {
                bWritten = AccountProtocol::MessageTypes::write( body, AccountGrantReply{ completion._grant, completion._result._result } );
                break;
            }
        }
        const bool bOk = completion._result == LoginResult::Ok;
        // 응답 형식 셋의 보존 판정은 결과 하나가 정한다(`AccountProtocol::isRetryable`) — 다시 시도할 결과를 기억하면 같은 키의 재시도가 같은 답만 받는다.
        const NetResponseRetention retention = AccountProtocol::isRetryable( completion._result._result ) ? NetResponseRetention::Discard : NetResponseRetention::Keep;
        if ( bWritten == false )
        {
            // 응답은 계정을 연결에 묶은 뒤에 보내야 하므로 먼저 써 둔다. 쓰지 못하면 같은 약속(kInternal)으로 응답한다.
            // 응답 몸은 저장소 작업이 만든 세션 토큰을 실으므로 작업 전에 쓸 수 없다. 클라이언트가 받지 못한 세션은 로그아웃 작업으로 지운다.
            // 그 작업의 완료는 짝이 되는 요청이 없어 버려진다.
            if ( bOk && AccountServerInternal::isGrantOperation( completion._operation ) )
                _pLoginService->logout( completion._grant._token, _nowMs, 0 );
            (void)_calls.fail( callID, OnlineError::kInternal );
            return;
        }

        if ( bOk && AccountServerInternal::isGrantOperation( completion._operation ) )
        {
            const AccountID accountID = completion._grant._identity._accountID;
            if ( host.bindAccount( call._connection, accountID ) == false )
            {
                // 같은 계정이 이 프로세스의 다른 연결에 접속해 있으면 새 로그인이 이전 연결을 밀어낸다. 사용자가 정한 정책이다.
                revokeBound( host, accountID, LoginRevokeReason::DuplicateLogin );
                if ( host.bindAccount( call._connection, accountID ) == false )
                {
                    // 그사이 이 연결이 닫혔으므로 세션은 재접속 유예로 둔다.
                    _pLoginService->markDisconnected( completion._grant._token._sessionID, _nowMs );
                    (void)_calls.respondBody( callID, body, retention, static_cast<int32>( completion._result._result ) );
                    return;
                }
            }
            _mapAccountToSession[accountID] = BoundSession{ completion._grant._token, call._connection };
            _presence.recordOnline( completion._grant._identity );
        }
        const bool bLinkOperation   = completion._operation == LoginOperation::LinkCredential || completion._operation == LoginOperation::LinkPlatform;
        const bool bPresenceRefresh = bOk && bLinkOperation && _mapAccountToSession.find( completion._identity._accountID ) != _mapAccountToSession.end();
        if ( bPresenceRefresh )
            _presence.recordOnline( completion._identity ); // 표시 이름이나 게스트 플래그가 바뀌었다
        (void)_calls.respondBody( callID, body, retention, static_cast<int32>( completion._result._result ) );
        if ( bOk && completion._operation == LoginOperation::Logout )
        {
            for ( auto sessionIt = _mapAccountToSession.begin(); sessionIt != _mapAccountToSession.end(); ++sessionIt )
            {
                if ( sessionIt->second._connection != call._connection )
                    continue;
                const AccountID accountID = sessionIt->first;
                _mapAccountToSession.erase( sessionIt );
                host.unbindAccount( accountID );
                break;
            }
        }
    }

    void AccountServer::handleEvent( OnlineServiceHost& host, const LoginEvent& event )
    {
        if ( event._kind != LoginEvent::Kind::Revoked )
            return;
        const auto sessionIt = _mapAccountToSession.find( event._accountID );
        if ( sessionIt == _mapAccountToSession.end() || sessionIt->second._token._sessionID != event._sessionID )
            return; // 이 프로세스에 그 세션의 연결이 없다. 이미 새 세션으로 바뀌었다
        revokeBound( host, event._accountID, event._reason );
    }

    bool AccountServer::respondCommonError( uint64 callID, LoginServiceResult result, const LoginGrant& grant )
    {
        if ( result.isFailed() == false )
            return false;
        BitWriter detail;
        switch ( result._errorCode )
        {
            case OnlineError::kRateLimited:
            {
                detail.writeVarUint( static_cast<uint64>( grant._retryAfterMs < 0 ? 0 : grant._retryAfterMs ) );
                (void)_calls.fail( callID, OnlineError::kRateLimited, &detail );
                return true;
            }
            case OnlineError::kUpdateRequired:
            {
                if ( AccountProtocol::MessageTypes::write( detail, AccountUpdateDetail{ grant._storeURL } ) )
                    (void)_calls.fail( callID, OnlineError::kUpdateRequired, &detail );
                else
                    (void)_calls.fail( callID, OnlineError::kUpdateRequired ); // 상점 주소가 상한을 넘어 write 가 로그를 남겼다. 결과 코드는 그대로 보낸다
                return true;
            }
            default:
            {
                (void)_calls.fail( callID, result._errorCode ); // 저장소 장애 · 깨진 본문 등 — 자세한 몸이 없다
                return true;
            }
        }
    }

    void AccountServer::revokeBound( OnlineServiceHost& host, AccountID accountID, LoginRevokeReason reason )
    {
        _mapAccountToSession.erase( accountID );
        string     reasonCode;
        const auto codeIt = _mapAccountToReasonCode.find( accountID );
        if ( codeIt != _mapAccountToReasonCode.end() )
        {
            reasonCode = codeIt->second;
            _mapAccountToReasonCode.erase( codeIt );
        }
        BitWriter push;
        if ( AccountProtocol::MessageTypes::write( push, AccountRevokedPush{ reasonCode, reason } ) ) // 쓰지 못했으면 write 가 로그를 남겼고, 연결은 아래에서 그대로 끊는다
            (void)host.sendPush( accountID, AccountMethod::Revoked::kKind, push );
        host.unbindAccount( accountID );
    }

    void AccountServer::attachHost( OnlineServiceHost& host )
    {
        _bHostAttached = SW_TRUE;
        _presence.attach( &host );
        if ( _presence.isEnabled() == false )
            return; // 서버가 한 대이다
        _listBusSubscription.push_back( host.subscribeServerBus( AccountBus::kRevokeTopic.getName(), this ) );
        _listBusSubscription.push_back( host.subscribeServerBus( OnlinePresence::makePushTopic( host.getServerBus()->getServerID() ), this ) );
    }

    void AccountServer::publishRemoteRevocations( OnlineServiceHost& host )
    {
        _listEventScratch.clear();
        _pLoginService->pollRemoteRevocations( _listEventScratch );
        ServerBus* pBus = host.getServerBus();
        for ( const LoginEvent& event : _listEventScratch )
        {
            string     reasonCode;
            const auto codeIt = _mapAccountToReasonCode.find( event._accountID );
            if ( codeIt != _mapAccountToReasonCode.end() )
            {
                reasonCode = codeIt->second;
                _mapAccountToReasonCode.erase( codeIt );
            }
            if ( pBus == nullptr )
                continue;                                                                                                                     // 서버가 한 대이므로 접속해 있지 않은 세션(재접속 유예)은 알릴 곳이 없다
            (void)AccountBus::kRevokeTopic.publish( *pBus, AccountRevoked{ reasonCode, event._accountID, event._sessionID, event._reason } ); // 실패는 발행이 오류 로그로 남겼다
        }
    }
} // namespace sw
