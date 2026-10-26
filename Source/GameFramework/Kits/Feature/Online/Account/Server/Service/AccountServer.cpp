#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountProtocol.h"

namespace sw
{
    SW_LOG_CALLER( "AccountServer" );

    namespace
    {
        struct AccountServerInternal
        {
            static bool isLoginMethod( uint16 method )
            {
                return method == AccountMethod::kLogin || method == AccountMethod::kGuestLogin || method == AccountMethod::kPlatformLogin ||
                       method == AccountMethod::kResume;
            }

            static bool isGrantOperation( LoginOperation operation )
            {
                return operation == LoginOperation::Login || operation == LoginOperation::GuestLogin || operation == LoginOperation::PlatformLogin ||
                       operation == LoginOperation::Resume;
            }

            /** @brief 요청 본문을 읽어 로그인 서비스로 보낸 결과이다. */
            enum class DispatchStatus : uint8
            {
                Ok,
                UnknownMethod,
                Malformed,
            };

            /** @brief `TRequest` 를 읽고, 읽기에 성공하면 `call` 에 넘긴다. 읽기에 실패하면 서비스를 부르지 않는다. */
            template <typename TRequest, typename TCall>
            static DispatchStatus readThen( BitReader& body, TCall&& call )
            {
                TRequest request;
                if ( AccountProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
                    return DispatchStatus::Malformed;
                call( request );
                return DispatchStatus::Ok;
            }

            /**
             * @brief 메서드에 맞는 본문을 읽어 로그인 서비스의 해당 함수를 부른다.
             * @details `pToken` 은 로그인한 세션의 토큰이다. 로그인 전 메서드(등록 · 로그인 · 재개)는 쓰지 않으므로 nullptr 이어도 된다.
             *          메서드마다 읽는 요청이 달라 경우마다 한 줄씩 부르는 짧은 디스패처라 한 함수로 둔다.
             */
            static DispatchStatus dispatch( LoginService& loginService, const OnlineCallContext& context, const LoginSessionToken* pToken, uint64 tag, BitReader& body )
            {
                const int64 nowMs = context._nowMs;
                switch ( context._method )
                {
                    case AccountMethod::kRegister:
                    {
                        return readThen<AccountCredentialRequest>( body, [&]( const AccountCredentialRequest& request )
                        { loginService.registerAccount( LoginCredential{ request._loginName, request._password }, nowMs, tag ); } );
                    }
                    case AccountMethod::kLogin:
                    {
                        return readThen<AccountLoginRequest>( body, [&]( const AccountLoginRequest& request )
                        { loginService.login( LoginCredential{ request._loginName, request._password }, request._clientInfo, context._remoteKey, nowMs, tag ); } );
                    }
                    case AccountMethod::kGuestLogin:
                    {
                        return readThen<AccountGuestLoginRequest>( body, [&]( const AccountGuestLoginRequest& request )
                        { loginService.guestLogin( request._arrDeviceSecret, request._clientInfo, context._remoteKey, nowMs, tag ); } );
                    }
                    case AccountMethod::kPlatformLogin:
                    {
                        return readThen<AccountPlatformRequest>( body, [&]( const AccountPlatformRequest& request )
                        { loginService.platformLogin( request._provider, request._ticketBytes, request._clientInfo, context._remoteKey, nowMs, tag ); } );
                    }
                    case AccountMethod::kResume:
                    {
                        return readThen<AccountResumeRequest>( body, [&]( const AccountResumeRequest& request )
                        { loginService.resumeSession( request._token, request._clientInfo, context._remoteKey, nowMs, tag ); } );
                    }
                    case AccountMethod::kLogout:
                    {
                        loginService.logout( *pToken, nowMs, tag );
                        return DispatchStatus::Ok;
                    }
                    case AccountMethod::kLinkCredential:
                    {
                        return readThen<AccountCredentialRequest>( body, [&]( const AccountCredentialRequest& request )
                        { loginService.linkCredential( *pToken, LoginCredential{ request._loginName, request._password }, nowMs, tag ); } );
                    }
                    case AccountMethod::kLinkPlatform:
                    {
                        return readThen<AccountPlatformRequest>( body, [&]( const AccountPlatformRequest& request )
                        { loginService.linkPlatform( *pToken, request._provider, request._ticketBytes, nowMs, tag ); } );
                    }
                    case AccountMethod::kIssueGameTicket:
                    {
                        AccountGameTicketRequest request;
                        if ( AccountProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok || request._serverID.empty() )
                            return DispatchStatus::Malformed; // 서버 id 가 없으면 발급할 곳이 없다
                        loginService.issueGameTicket( *pToken, hashed_string( request._serverID ), nowMs, tag );
                        return DispatchStatus::Ok;
                    }
                    case AccountMethod::kUnlinkPlatform:
                    {
                        return readThen<AccountProviderRequest>( body, [&]( const AccountProviderRequest& request )
                        { loginService.unlinkPlatform( *pToken, request._provider, nowMs, tag ); } );
                    }
                    case AccountMethod::kListLinks:
                    {
                        loginService.listLinks( *pToken, nowMs, tag );
                        return DispatchStatus::Ok;
                    }
                    case AccountMethod::kRequestDeletion:
                    {
                        loginService.requestDeletion( *pToken, nowMs, tag );
                        return DispatchStatus::Ok;
                    }
                    case AccountMethod::kCancelDeletion:
                    {
                        loginService.cancelDeletion( *pToken, nowMs, tag );
                        return DispatchStatus::Ok;
                    }
                    default:
                    {
                        return DispatchStatus::UnknownMethod;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AccountServer::AccountServer()
        : _mapTagToCall{}
        , _mapAccountToSession{}
        , _mapAccountToReasonCode{}
        , _listCompletionScratch{}
        , _listEventScratch{}
        , _listBusSubscription{}
        , _presence{}
        , _settings{}
        , _pLoginService{ nullptr }
        , _nextTag{ 1 }
        , _nowMs{ 0 }
        , _nextPurgeMs{ 0 }
        , _nextRefreshMs{ 0 }
        , _bHostAttached{ SW_FALSE }
    {
    }

    void AccountServer::initialize( LoginService* pLoginService, const AccountServerConfig& settings )
    {
        _pLoginService = pLoginService;
        _settings      = settings;
        _presence.setSettings( settings._presence );
    }

    void AccountServer::shutdown()
    {
        _mapTagToCall.clear();
        _mapAccountToSession.clear();
        _mapAccountToReasonCode.clear();
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

    const OnlineProtocolEntry& AccountServer::getProtocol() const
    {
        return AccountProtocol::kOnlineEntry;
    }

    bool AccountServer::isAnonymousMethod( uint16 method ) const
    {
        return method == AccountMethod::kRegister || AccountServerInternal::isLoginMethod( method );
    }

    void AccountServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        _nowMs = context._nowMs;
        if ( AccountServerInternal::isLoginMethod( context._method ) && context._accountID != kInvalidAccountID )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest ); // 한 연결에는 계정 하나만 쓴다
            return;
        }
        const auto          sessionIt     = _mapAccountToSession.find( context._accountID );
        const BoundSession* pSession      = sessionIt != _mapAccountToSession.end() ? &sessionIt->second : nullptr;
        const bool          bNeedsSession = isAnonymousMethod( context._method ) == false;
        if ( bNeedsSession && pSession == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnauthenticated );
            return;
        }
        const uint64 tag = _nextTag++;
        switch ( AccountServerInternal::dispatch( *_pLoginService, context, pSession != nullptr ? &pSession->_token : nullptr, tag, body ) )
        {
            case AccountServerInternal::DispatchStatus::UnknownMethod:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
            case AccountServerInternal::DispatchStatus::Malformed:
            {
                (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                return;
            }
            case AccountServerInternal::DispatchStatus::Ok:
            {
                break;
            }
        }
        _mapTagToCall[tag] = PendingCall{ context._token, context._connection, context._method };
    }

    void AccountServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        _nowMs = nowMs;
        if ( _bHostAttached == SW_FALSE )
            attachHost( host );
        _pLoginService->update( nowMs );
        _listCompletionScratch.clear();
        _pLoginService->drainCompletions( _listCompletionScratch );
        for ( const LoginCompletion& completion : _listCompletionScratch )
        {
            handleCompletion( host, completion );
        }
        // 이벤트는 완료 뒤에 처리한다. 로그인 응답과 탈퇴 응답을 먼저 보내고, 밀려난 세션은 그 뒤에 끊는다.
        _listEventScratch.clear();
        _pLoginService->drainEvents( _listEventScratch );
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
        _presence.noteOffline( accountID );
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
        AccountRevokeNotice notice;
        if ( AccountBus::kRevokeTopic.read( message, notice ) != ServerBusReadResult::Read )
            return; // 읽기가 (타입, 결과, 버전)마다 한 번 오류 로그와 카운터를 남겼다 — 그 세션은 영속 레코드 다시 읽기로 끊긴다
        if ( notice._reasonCode.empty() == false && findSessionID( notice._accountID ) == notice._sessionID )
            _mapAccountToReasonCode[notice._accountID] = notice._reasonCode;
        _pLoginService->noteRevokedElsewhere( notice._accountID, notice._sessionID, notice._reason );
    }

    void AccountServer::revokeAccountSessions( AccountID accountID, string_view reasonCode, int64 nowMs )
    {
        _mapAccountToReasonCode[accountID] = string( reasonCode );
        _pLoginService->revokeAccountSessions( accountID, LoginRevokeReason::Administrative, nowMs, _nextTag++ ); // 짝이 되는 요청 태그가 없으므로 완료는 버리고 이벤트로 끊는다
    }

    uint64 AccountServer::findSessionID( AccountID accountID ) const
    {
        const auto sessionIt = _mapAccountToSession.find( accountID );
        return sessionIt == _mapAccountToSession.end() ? 0 : sessionIt->second._token._sessionID;
    }

    void AccountServer::handleCompletion( OnlineServiceHost& host, const LoginCompletion& completion )
    {
        const auto callIt = _mapTagToCall.find( completion._requestTag );
        if ( callIt == _mapTagToCall.end() )
            return;
        const PendingCall call = callIt->second;
        _mapTagToCall.erase( callIt );
        if ( respondCommonError( host, call._token, completion._result, completion._grant ) )
            return;

        BitWriter body;
        bool      bWritten = false;
        switch ( completion._operation )
        {
            case LoginOperation::IssueGameTicket:
            {
                bWritten = AccountProtocol::MessageTypes::write( body, AccountTicketReply{ completion._ticket, completion._result } );
                break;
            }
            case LoginOperation::ListLinks:
            {
                bWritten = AccountProtocol::MessageTypes::write( body, AccountLinkReply{ completion._linkSummary, completion._result } );
                break;
            }
            case LoginOperation::Register:
            case LoginOperation::LinkCredential:
            case LoginOperation::LinkPlatform:
            {
                AccountGrantReply reply{ completion._grant, completion._result };
                reply._grant._identity = completion._identity;
                bWritten               = AccountProtocol::MessageTypes::write( body, reply );
                break;
            }
            default:
            {
                bWritten = AccountProtocol::MessageTypes::write( body, AccountGrantReply{ completion._grant, completion._result } );
                break;
            }
        }
        const bool bOk = completion._result == LoginResult::Ok;
        if ( bWritten == false )
        {
            // 응답은 계정을 연결에 묶은 뒤에 보내야 하므로 respondMessage 를 쓰지 않고 먼저 써 둔다. 쓰지 못하면 같은 약속(kInternal)으로 응답한다.
            // 응답 몸은 저장소 작업이 만든 세션 토큰을 실으므로 작업 전에 쓸 수 없다. 클라이언트가 받지 못한 세션은 로그아웃 작업으로 지운다.
            // 그 작업의 완료는 짝이 되는 요청이 없어 버려진다.
            if ( bOk && AccountServerInternal::isGrantOperation( completion._operation ) )
                _pLoginService->logout( completion._grant._token, _nowMs, _nextTag++ );
            (void)host.respondError( call._token, OnlineError::kInternal );
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
                    (void)host.respondOk( call._token, body );
                    return;
                }
            }
            _mapAccountToSession[accountID] = BoundSession{ completion._grant._token, call._connection };
            _presence.noteOnline( completion._grant._identity );
        }
        const bool bLinkOperation   = completion._operation == LoginOperation::LinkCredential || completion._operation == LoginOperation::LinkPlatform;
        const bool bPresenceRefresh = bOk && bLinkOperation && _mapAccountToSession.find( completion._identity._accountID ) != _mapAccountToSession.end();
        if ( bPresenceRefresh )
            _presence.noteOnline( completion._identity ); // 표시 이름이나 게스트 플래그가 바뀌었다
        (void)host.respondOk( call._token, body );
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

    bool AccountServer::respondCommonError( OnlineServiceHost& host, const NetRequestToken& token, LoginResult result, const LoginGrant& grant )
    {
        BitWriter detail;
        switch ( result )
        {
            case LoginResult::RateLimited:
            {
                detail.writeVarUint( static_cast<uint64>( grant._retryAfterMs < 0 ? 0 : grant._retryAfterMs ) );
                (void)host.respondError( token, OnlineError::kRateLimited, &detail );
                return true;
            }
            case LoginResult::StoreUnavailable:
            {
                (void)host.respondError( token, OnlineError::kUnavailable );
                return true;
            }
            case LoginResult::UpdateRequired:
            {
                if ( AccountProtocol::MessageTypes::write( detail, AccountUpdateDetail{ grant._storeURL } ) )
                    (void)host.respondError( token, OnlineError::kUpdateRequired, &detail );
                else
                    (void)host.respondError( token, OnlineError::kUpdateRequired ); // 상점 주소가 상한을 넘어 write 가 로그를 남겼다. 결과 코드는 그대로 보낸다
                return true;
            }
            default:
            {
                return false;
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
            (void)host.sendPush( accountID, AccountMethod::kPushRevoked, push );
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
        _pLoginService->drainRemoteRevocations( _listEventScratch );
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
                continue;                                                                                                                          // 서버가 한 대이므로 접속해 있지 않은 세션(재접속 유예)은 알릴 곳이 없다
            (void)AccountBus::kRevokeTopic.publish( *pBus, AccountRevokeNotice{ reasonCode, event._accountID, event._sessionID, event._reason } ); // 실패는 발행이 오류 로그로 남겼다
        }
    }
} // namespace sw
