#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/AccountServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Online/Account/AccountProtocol.h"

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

    void AccountServer::initialize( LoginService* pLoginService, const AccountServerSettings& settings )
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
        _presence.shutdown();
        _pLoginService = nullptr;
    }

    void AccountServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _presence.detach(); // 캐시 답은 호스트가 이미 Unavailable 로 거뒀다
        _bHostAttached = SW_FALSE;
    }

    uint32 AccountServer::getProtocolVersion() const { return AccountProtocol::kVersion; }

    bool AccountServer::isAnonymousMethod( uint16 method ) const { return method == AccountMethod::kRegister || AccountServerInternal::isLoginMethod( method ); }

    void AccountServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        _nowMs = context._nowMs;
        if ( AccountServerInternal::isLoginMethod( context._method ) && context._accountId != kInvalidAccountId )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest ); // 한 연결에 계정 하나
            return;
        }
        const auto          sessionIt     = _mapAccountToSession.find( context._accountId );
        const BoundSession* pSession      = sessionIt != _mapAccountToSession.end() ? &sessionIt->second : nullptr;
        const bool          bNeedsSession = isAnonymousMethod( context._method ) == false;
        if ( bNeedsSession && pSession == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnauthenticated );
            return;
        }
        const uint64 tag      = _nextTag++;
        const int64  nowMs    = context._nowMs;
        bool         bDecoded = true;
        switch ( context._method )
        {
            case AccountMethod::kRegister:
            {
                LoginCredential credential;
                bDecoded = AccountWire::readCredential( body, credential._loginName, credential._password );
                if ( bDecoded )
                    _pLoginService->registerAccount( credential, nowMs, tag );
                break;
            }
            case AccountMethod::kLogin:
            {
                LoginCredential   credential;
                AccountClientInfo clientInfo;
                bDecoded = AccountWire::readCredential( body, credential._loginName, credential._password ) && AccountWire::readClientInfo( body, clientInfo );
                if ( bDecoded )
                    _pLoginService->login( credential, clientInfo, context._remoteKey, nowMs, tag );
                break;
            }
            case AccountMethod::kGuestLogin:
            {
                uint8             arrSecret[LoginConstant::kDeviceSecretSize];
                AccountClientInfo clientInfo;
                bDecoded = body.readBytes( arrSecret, LoginConstant::kDeviceSecretSize ) && AccountWire::readClientInfo( body, clientInfo );
                if ( bDecoded )
                    _pLoginService->guestLogin( arrSecret, clientInfo, context._remoteKey, nowMs, tag );
                break;
            }
            case AccountMethod::kPlatformLogin:
            {
                string            provider;
                vector<uint8>     ticketBytes;
                AccountClientInfo clientInfo;
                bDecoded = AccountWire::readText( body, LoginConstant::kMaxProviderNameSize, provider ) &&
                           AccountWire::readBlob( body, LoginConstant::kMaxPlatformTicketSize, ticketBytes ) && AccountWire::readClientInfo( body, clientInfo );
                if ( bDecoded )
                    _pLoginService->platformLogin( provider, ticketBytes, clientInfo, context._remoteKey, nowMs, tag );
                break;
            }
            case AccountMethod::kResume:
            {
                LoginSessionToken token;
                AccountClientInfo clientInfo;
                bDecoded = AccountWire::readToken( body, token ) && AccountWire::readClientInfo( body, clientInfo );
                if ( bDecoded )
                    _pLoginService->resumeSession( token, clientInfo, context._remoteKey, nowMs, tag );
                break;
            }
            case AccountMethod::kLogout:
            {
                _pLoginService->logout( pSession->_token, nowMs, tag );
                break;
            }
            case AccountMethod::kLinkCredential:
            {
                LoginCredential credential;
                bDecoded = AccountWire::readCredential( body, credential._loginName, credential._password );
                if ( bDecoded )
                    _pLoginService->linkCredential( pSession->_token, credential, nowMs, tag );
                break;
            }
            case AccountMethod::kLinkPlatform:
            {
                string        provider;
                vector<uint8> ticketBytes;
                bDecoded = AccountWire::readText( body, LoginConstant::kMaxProviderNameSize, provider ) &&
                           AccountWire::readBlob( body, LoginConstant::kMaxPlatformTicketSize, ticketBytes );
                if ( bDecoded )
                    _pLoginService->linkPlatform( pSession->_token, provider, ticketBytes, nowMs, tag );
                break;
            }
            case AccountMethod::kIssueGameTicket:
            {
                string serverId;
                bDecoded = AccountWire::readText( body, LoginConstant::kMaxDisplayNameSize, serverId ) && serverId.empty() == false;
                if ( bDecoded )
                    _pLoginService->issueGameTicket( pSession->_token, hashed_string( serverId ), nowMs, tag );
                break;
            }
            case AccountMethod::kUnlinkPlatform:
            {
                string provider;
                bDecoded = AccountWire::readText( body, LoginConstant::kMaxProviderNameSize, provider );
                if ( bDecoded )
                    _pLoginService->unlinkPlatform( pSession->_token, provider, nowMs, tag );
                break;
            }
            case AccountMethod::kListLinks:
            {
                _pLoginService->listLinks( pSession->_token, nowMs, tag );
                break;
            }
            case AccountMethod::kRequestDeletion:
            {
                _pLoginService->requestDeletion( pSession->_token, nowMs, tag );
                break;
            }
            case AccountMethod::kCancelDeletion:
            {
                _pLoginService->cancelDeletion( pSession->_token, nowMs, tag );
                break;
            }
            default:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
        }
        if ( bDecoded == false || body.hasOverflowed() )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        _mapTagToCall[tag] = PendingCall{ context._token, context._connection, context._method };
    }

    void AccountServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        _nowMs = nowMs;
        if ( _bHostAttached == SW_FALSE )
            attachHost( host );
        _pLoginService->tick( nowMs );
        _listCompletionScratch.clear();
        _pLoginService->drainCompletions( _listCompletionScratch );
        for ( const LoginCompletion& completion : _listCompletionScratch )
            handleCompletion( host, completion );
        // 완료 뒤에 사건 — 로그인 응답 · 탈퇴 응답을 먼저 보내고, 밀려난 세션은 그 뒤 끊는다.
        _listEventScratch.clear();
        _pLoginService->drainEvents( _listEventScratch );
        for ( const LoginEvent& event : _listEventScratch )
            handleEvent( host, event );
        publishRemoteRevocations( host );
        _presence.tick( nowMs );
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

    void AccountServer::onAccountLeft( OnlineServiceHost& host, AccountId accountId )
    {
        (void)host;
        _presence.noteOffline( accountId );
        const auto sessionIt = _mapAccountToSession.find( accountId );
        if ( sessionIt == _mapAccountToSession.end() )
            return; // 이 객체가 끊은 연결(밀려남 · 로그아웃) — 세션 표에서 먼저 뺐다
        const uint64 sessionId = sessionIt->second._token._sessionId;
        _mapAccountToSession.erase( sessionIt );
        _pLoginService->markDisconnected( sessionId, _nowMs ); // 클라이언트가 끊겼다 — 재접속 유예
    }

    void AccountServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        if ( message._topic != kRevokeTopic )
        {
            (void)_presence.handlePushMessage( message );
            return;
        }
        if ( host.getServerBus() != nullptr && message._originServerId == host.getServerBus()->getServerId() )
            return; // 이 서버가 보낸 것 — 이미 끊었다
        BitReader       reader( message._bytes.data(), static_cast<int32>( message._bytes.size() ) );
        const AccountId accountId = reader.readVarUint();
        const uint64    sessionId = reader.readVarUint();
        const uint64    reason    = reader.readVarUint();
        string          reasonCode;
        if ( ServiceKeyUtil::readString( reader, LoginConstant::kMaxReasonCodeSize, reasonCode ) == false || reader.hasOverflowed() ||
             reason > static_cast<uint64>( LoginRevokeReason::AccountDeleted ) )
        {
            SW_LOG_WARNING( "Malformed account revoke bus message from server %#", message._originServerId );
            return;
        }
        if ( reasonCode.empty() == false && findSessionId( accountId ) == sessionId )
            _mapAccountToReasonCode[accountId] = reasonCode;
        _pLoginService->noteRevokedElsewhere( accountId, sessionId, static_cast<LoginRevokeReason>( reason ) );
    }

    void AccountServer::revokeAccountSessions( AccountId accountId, string_view reasonCode, int64 nowMs )
    {
        _mapAccountToReasonCode[accountId] = string( reasonCode );
        _pLoginService->revokeAccountSessions( accountId, LoginRevokeReason::Administrative, nowMs, _nextTag++ ); // 꼬리표에 짝이 없다 — 완료는 버리고 사건으로 끊는다
    }

    uint64 AccountServer::findSessionId( AccountId accountId ) const
    {
        const auto sessionIt = _mapAccountToSession.find( accountId );
        return sessionIt == _mapAccountToSession.end() ? 0 : sessionIt->second._token._sessionId;
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
        switch ( completion._operation )
        {
            case LoginOperation::IssueGameTicket:
            {
                AccountWire::writeTicketReply( body, completion._result, completion._ticket );
                break;
            }
            case LoginOperation::ListLinks:
            {
                AccountWire::writeLinkReply( body, completion._result, completion._linkSummary );
                break;
            }
            case LoginOperation::Register:
            case LoginOperation::LinkCredential:
            case LoginOperation::LinkPlatform:
            {
                LoginGrant grant = completion._grant;
                grant._identity  = completion._identity;
                AccountWire::writeGrantReply( body, completion._result, grant );
                break;
            }
            default:
            {
                AccountWire::writeGrantReply( body, completion._result, completion._grant );
                break;
            }
        }

        const bool bOk = completion._result == LoginResult::Ok;
        if ( bOk && AccountServerInternal::isGrantOperation( completion._operation ) )
        {
            const AccountId accountId = completion._grant._identity._accountId;
            if ( host.bindAccount( call._connection, accountId ) == false )
            {
                // 같은 계정이 이 프로세스의 다른 연결에 붙어 있다 — 새 로그인이 옛 연결을 밀어낸다(사용자 결정).
                revokeBound( host, accountId, LoginRevokeReason::DuplicateLogin );
                if ( host.bindAccount( call._connection, accountId ) == false )
                {
                    // 그새 이 연결이 닫혔다 — 세션은 재접속 유예로 둔다.
                    _pLoginService->markDisconnected( completion._grant._token._sessionId, _nowMs );
                    (void)host.respondOk( call._token, body );
                    return;
                }
            }
            _mapAccountToSession[accountId] = BoundSession{ completion._grant._token, call._connection };
            _presence.noteOnline( completion._grant._identity );
        }
        const bool bLinkOperation = completion._operation == LoginOperation::LinkCredential || completion._operation == LoginOperation::LinkPlatform;
        if ( bOk && bLinkOperation && _mapAccountToSession.find( completion._identity._accountId ) != _mapAccountToSession.end() )
            _presence.noteOnline( completion._identity ); // 표시 이름 · 게스트 깃발이 바뀌었다
        (void)host.respondOk( call._token, body );
        if ( bOk && completion._operation == LoginOperation::Logout )
        {
            for ( auto sessionIt = _mapAccountToSession.begin(); sessionIt != _mapAccountToSession.end(); ++sessionIt )
            {
                if ( sessionIt->second._connection != call._connection )
                    continue;
                const AccountId accountId = sessionIt->first;
                _mapAccountToSession.erase( sessionIt );
                host.unbindAccount( accountId );
                break;
            }
        }
    }

    void AccountServer::handleEvent( OnlineServiceHost& host, const LoginEvent& event )
    {
        if ( event._kind != LoginEvent::Kind::Revoked )
            return;
        const auto sessionIt = _mapAccountToSession.find( event._accountId );
        if ( sessionIt == _mapAccountToSession.end() || sessionIt->second._token._sessionId != event._sessionId )
            return; // 이 프로세스에 그 세션의 연결이 없다(이미 새 세션으로 바뀌었다)
        revokeBound( host, event._accountId, event._reason );
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
                AccountWire::writeText( detail, grant._storeUrl );
                (void)host.respondError( token, OnlineError::kUpdateRequired, &detail );
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    void AccountServer::revokeBound( OnlineServiceHost& host, AccountId accountId, LoginRevokeReason reason )
    {
        _mapAccountToSession.erase( accountId );
        string     reasonCode;
        const auto codeIt = _mapAccountToReasonCode.find( accountId );
        if ( codeIt != _mapAccountToReasonCode.end() )
        {
            reasonCode = codeIt->second;
            _mapAccountToReasonCode.erase( codeIt );
        }
        BitWriter push;
        AccountWire::writeRevokedPush( push, reason, reasonCode );
        (void)host.sendPush( accountId, AccountMethod::kPushRevoked, push );
        host.unbindAccount( accountId );
    }

    void AccountServer::attachHost( OnlineServiceHost& host )
    {
        _bHostAttached = SW_TRUE;
        _presence.attach( &host );
        if ( _presence.isEnabled() == false )
            return; // 서버 한 대
        host.subscribeServerBus( kRevokeTopic, this );
        host.subscribeServerBus( OnlinePresence::makePushTopic( host.getServerBus()->getServerId() ), this );
    }

    void AccountServer::publishRemoteRevocations( OnlineServiceHost& host )
    {
        _listEventScratch.clear();
        _pLoginService->drainRemoteRevocations( _listEventScratch );
        IServerBus* pBus = host.getServerBus();
        for ( const LoginEvent& event : _listEventScratch )
        {
            string     reasonCode;
            const auto codeIt = _mapAccountToReasonCode.find( event._accountId );
            if ( codeIt != _mapAccountToReasonCode.end() )
            {
                reasonCode = codeIt->second;
                _mapAccountToReasonCode.erase( codeIt );
            }
            if ( pBus == nullptr )
                continue; // 서버 한 대 — 붙어 있지 않은 세션(재접속 유예)은 알릴 곳이 없다
            BitWriter message;
            message.writeVarUint( event._accountId );
            message.writeVarUint( event._sessionId );
            message.writeVarUint( static_cast<uint64>( event._reason ) );
            ServiceKeyUtil::writeString( message, reasonCode );
            pBus->publish( kRevokeTopic, message.getBytes().data(), message.getByteCount() );
        }
    }
} // namespace sw
