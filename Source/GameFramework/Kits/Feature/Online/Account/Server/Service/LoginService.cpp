#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"

#include "Core/Container/StringUtil.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "LoginService" );

    namespace
    {
        struct LoginServiceInternal
        {
            static constexpr utf8 kMinimumBuildKey[]     = "account.minimum_build.";
            static constexpr utf8 kRecommendedBuildKey[] = "account.recommended_build.";
            static constexpr utf8 kStoreURLKey[]         = "account.store_url.";

            /** @brief 돌아가며 다시 읽는 차례가 해시맵 순서에 기대지 않게(결정적) 계정 id 로 정렬한다. */
            struct SessionRefLess
            {
                bool operator()( const LoginSessionRef& left, const LoginSessionRef& right ) const { return left._accountId < right._accountId; }
            };

            static string makeNameKey( string_view displayName ) { return StringUtil::toLower( string( displayName ).c_str() ); }

            static string makeConfigKey( const utf8* pPrefix, string_view platform )
            {
                string key{ pPrefix };
                key += platform;
                return key;
            }

            /** @brief 로그인 요청 하나를 저장소 스레드에서 돌리고, 결과를 서비스에 돌려줍니다. 입력은 모두 복사해 든다. */
            class LoginWork final : public IServiceStoreWork
            {
            public:
                LoginCredential   _credential;
                LoginSessionToken _token;
                hashed_string     _serverId;
                string            _provider;
                string            _subject;
                string            _displayNameHint;
                string            _storeURL;
                uint64            _accountId;
                LoginRevokeReason _revokeReason;
                uint8             _arrDeviceSecret[LoginConstant::kDeviceSecretSize];
                uint8             _bUpdateRecommended;

                LoginWork( LoginService* pService, ILoginCrypto* pCrypto, const LoginSettings& settings, LoginOperation operation, uint64 requestTag, int64 nowMs )
                    : _credential{}
                    , _token{}
                    , _serverId{}
                    , _provider{}
                    , _subject{}
                    , _displayNameHint{}
                    , _storeURL{}
                    , _accountId{ 0 }
                    , _revokeReason{ LoginRevokeReason::Administrative }
                    , _arrDeviceSecret{}
                    , _bUpdateRecommended{ SW_FALSE }
                    , _outcome{}
                    , _completion{}
                    , _settings{ settings }
                    , _pService{ pService }
                    , _pCrypto{ pCrypto }
                    , _nowMs{ nowMs }
                {
                    _completion._operation  = operation;
                    _completion._requestTag = requestTag;
                }

                void run( IServiceStoreConnection& connection ) override
                {
                    LoginStoreLogic logic{ connection, *_pCrypto, _settings, _pService->getTicketAuthority(), _outcome };
                    switch ( _completion._operation )
                    {
                        case LoginOperation::Register:
                        {
                            uint64 accountId                   = 0;
                            _completion._result                = logic.registerAccount( _credential, &accountId );
                            _completion._identity._accountId   = accountId;
                            _completion._identity._displayName = _credential._loginName;
                            break;
                        }
                        case LoginOperation::Login:
                        {
                            _completion._result = logic.login( _credential, _nowMs, _completion._grant );
                            break;
                        }
                        case LoginOperation::GuestLogin:
                        {
                            _completion._result = logic.guestLogin( _arrDeviceSecret, _nowMs, _completion._grant );
                            break;
                        }
                        case LoginOperation::PlatformLogin:
                        {
                            _completion._result = logic.platformLogin( _provider, _subject, _displayNameHint, _nowMs, _completion._grant );
                            break;
                        }
                        case LoginOperation::Resume:
                        {
                            _completion._result = logic.resumeSession( _token, _nowMs, _completion._grant );
                            break;
                        }
                        case LoginOperation::Validate:
                        {
                            _completion._result = logic.validateSession( _token, _nowMs, _completion._identity, &_completion._grant._revokeReason );
                            break;
                        }
                        case LoginOperation::Logout:
                        {
                            _completion._result = logic.logout( _token, _nowMs );
                            break;
                        }
                        case LoginOperation::IssueGameTicket:
                        {
                            _completion._result = logic.issueGameTicket( _token, _serverId, _nowMs, _completion._ticket );
                            break;
                        }
                        case LoginOperation::Revoke:
                        {
                            _completion._result = logic.revokeAccountSessions( _accountId, _revokeReason, _nowMs );
                            break;
                        }
                        case LoginOperation::LinkCredential:
                        {
                            _completion._result = logic.linkCredential( _token, _credential, _nowMs, _completion._identity );
                            break;
                        }
                        case LoginOperation::LinkPlatform:
                        {
                            _completion._result = logic.linkPlatform( _token, _provider, _subject, _nowMs, _completion._identity );
                            break;
                        }
                        case LoginOperation::UnlinkPlatform:
                        {
                            _completion._result = logic.unlinkPlatform( _token, _provider, _nowMs );
                            break;
                        }
                        case LoginOperation::ListLinks:
                        {
                            _completion._result = logic.listLinks( _token, _nowMs, _completion._linkSummary );
                            break;
                        }
                        case LoginOperation::RequestDeletion:
                        {
                            _completion._result = logic.requestDeletion( _token, _nowMs, _completion._grant._deletionDueMs );
                            break;
                        }
                        case LoginOperation::CancelDeletion:
                        {
                            _completion._result = logic.cancelDeletion( _token, _nowMs );
                            break;
                        }
                    }
                    if ( _completion._result == LoginResult::Ok && _bUpdateRecommended == SW_TRUE )
                    {
                        _completion._grant._bUpdateRecommended = SW_TRUE;
                        _completion._grant._storeURL           = _storeURL;
                    }
                    // 워커를 떠나면 비밀번호 · 장치 비밀 · 외부 주체를 들고 있지 않는다(지우기 · wipe 는 하지 않는다 — 서버 관례).
                    _credential._password.clear();
                    _subject.clear();
                    Memory::set( _arrDeviceSecret, 0, sizeof( _arrDeviceSecret ) );
                }

                void complete() override { _pService->applyCompletion( std::move( _completion ), _outcome ); }

            private:
                LoginStoreOutcome _outcome;
                LoginCompletion   _completion;
                LoginSettings     _settings; ///< 복사 — 서비스 설정이 바뀌어도 이 일은 맡긴 때의 값으로 돈다
                LoginService*     _pService;
                ILoginCrypto*     _pCrypto;
                int64             _nowMs;
            };

            /** @brief 끊김 · 세션 다시 읽기 · 탈퇴 쓸기 — 완료가 없고 부작용만 있는 일입니다. */
            class LoginMaintenanceWork final : public IServiceStoreWork
            {
            public:
                vector<LoginSessionRef> _listOnline;
                uint64                  _disconnectedSessionId;
                int32                   _purgeCount;

                LoginMaintenanceWork( LoginService* pService, ILoginCrypto* pCrypto, const LoginSettings& settings, int64 nowMs )
                    : _listOnline{}
                    , _disconnectedSessionId{ 0 }
                    , _purgeCount{ 0 }
                    , _outcome{}
                    , _settings{ settings }
                    , _pService{ pService }
                    , _pCrypto{ pCrypto }
                    , _nowMs{ nowMs }
                {
                }

                void run( IServiceStoreConnection& connection ) override
                {
                    LoginStoreLogic logic{ connection, *_pCrypto, _settings, _pService->getTicketAuthority(), _outcome };
                    if ( _disconnectedSessionId != 0 )
                        logic.markDisconnected( _disconnectedSessionId, _nowMs );
                    if ( _listOnline.empty() == false )
                        logic.refreshSessions( _listOnline, _nowMs );
                    if ( _purgeCount > 0 )
                        (void)logic.purgeDueDeletions( _nowMs, _purgeCount );
                }

                void complete() override
                {
                    LoginCompletion none;
                    none._requestTag = 0; // 꼬리표 0 은 완료 목록에 쌓지 않는다(applyCompletion)
                    _pService->applyCompletion( std::move( none ), _outcome );
                }

            private:
                LoginStoreOutcome _outcome;
                LoginSettings     _settings;
                LoginService*     _pService;
                ILoginCrypto*     _pCrypto;
                int64             _nowMs;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoginService::LoginService()
        : _eventBuffer{}
        , _remoteRevokeBuffer{}
        , _completionBuffer{}
        , _ticketAuthority{}
        , _settings{}
        , _listPlatformProvider{}
        , _listPendingVerification{}
        , _listVerificationScratch{}
        , _mapAccountToSession{}
        , _mapAccountToIdentity{}
        , _mapNameKeyToAccount{}
        , _mapClientToAttemptBucket{}
        , _pStore{ nullptr }
        , _pCrypto{ nullptr }
        , _pRemoteConfig{ nullptr }
        , _refreshCursor{ 0 }
        , _pendingCount{ 0 }
    {
    }

    void LoginService::initialize( IServiceStore* pStore, ILoginCrypto* pCrypto, const LoginSettings& settings,
                                   const uint8 ( &arrTicketMasterKey )[LoginTicketAuthority::kMasterKeySize] )
    {
        _pStore   = pStore;
        _pCrypto  = pCrypto;
        _settings = settings;
        _ticketAuthority.initialize( pCrypto, arrTicketMasterKey );
        _mapClientToAttemptBucket.initialize( settings._attemptBurst, settings._attemptRefillMs, kMaxTrackedClientCount );
    }

    void LoginService::shutdown()
    {
        _ticketAuthority.shutdown();
        _mapAccountToSession.clear();
        _mapAccountToIdentity.clear();
        _mapNameKeyToAccount.clear();
        _listPendingVerification.clear();
        _listPlatformProvider.clear();
        _eventBuffer.clear();
        _remoteRevokeBuffer.clear();
        _pStore        = nullptr;
        _pCrypto       = nullptr;
        _pRemoteConfig = nullptr;
    }

    bool LoginService::registerPlatformProvider( IPlatformLoginProvider* pProvider )
    {
        if ( pProvider == nullptr || AccountUtil::isValidLowerToken( pProvider->getName(), LoginConstant::kMaxProviderNameSize ) == false )
        {
            SW_LOG_ERROR( "platform login provider has no valid name" );
            return false;
        }
        if ( findPlatformProvider( pProvider->getName() ) != nullptr )
        {
            SW_LOG_ERROR( "platform login provider '%#' is already registered", pProvider->getName() );
            return false;
        }
        _listPlatformProvider.push_back( pProvider );
        return true;
    }

    void LoginService::tick( int64 nowMs )
    {
        for ( IPlatformLoginProvider* pProvider : _listPlatformProvider )
        {
            pProvider->tick( nowMs );
            _listVerificationScratch.clear();
            (void)pProvider->pollVerifications( _listVerificationScratch );
            for ( const PlatformLoginVerification& verification : _listVerificationScratch )
            {
                for ( size_t pendingIndex = 0; pendingIndex < _listPendingVerification.size(); ++pendingIndex )
                {
                    const PendingVerification& pending  = _listPendingVerification[pendingIndex];
                    const bool                 bMatches = pending._pProvider == pProvider && pending._verificationId == verification._verificationId;
                    if ( bMatches == false )
                        continue;
                    const PendingVerification finished = pending;
                    _listPendingVerification.erase( _listPendingVerification.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
                    finishVerification( finished, verification );
                    break;
                }
            }
        }
    }

    void LoginService::registerAccount( const LoginCredential& credential, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Register, requestTag, nowMs );
        work->_credential                                = credential;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::login( const LoginCredential& credential, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs, uint64 requestTag )
    {
        LoginGrant        buildGrant;
        const LoginResult build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::Login, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::Login, requestTag, LoginResult::RateLimited, buildGrant );
            return;
        }
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Login, requestTag, nowMs );
        work->_credential                                = credential;
        work->_bUpdateRecommended                        = buildGrant._bUpdateRecommended;
        work->_storeURL                                  = buildGrant._storeURL;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs,
                                   uint64 requestTag )
    {
        LoginGrant        buildGrant;
        const LoginResult build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::GuestLogin, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::GuestLogin, requestTag, LoginResult::RateLimited, buildGrant );
            return;
        }
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::GuestLogin, requestTag, nowMs );
        Memory::copy( work->_arrDeviceSecret, arrDeviceSecret, LoginConstant::kDeviceSecretSize );
        work->_bUpdateRecommended = buildGrant._bUpdateRecommended;
        work->_storeURL           = buildGrant._storeURL;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::platformLogin( string_view provider, const vector<uint8>& ticketBytes, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs,
                                      uint64 requestTag )
    {
        LoginGrant        buildGrant;
        const LoginResult build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::PlatformLogin, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::PlatformLogin, requestTag, LoginResult::RateLimited, buildGrant );
            return;
        }
        beginVerification( LoginOperation::PlatformLogin, provider, ticketBytes, LoginSessionToken{}, buildGrant, nowMs, requestTag );
    }

    void LoginService::resumeSession( const LoginSessionToken& token, const AccountClientInfo& clientInfo, uint64 clientKey, int64 nowMs, uint64 requestTag )
    {
        LoginGrant        buildGrant;
        const LoginResult build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::Resume, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::Resume, requestTag, LoginResult::RateLimited, buildGrant );
            return;
        }
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Resume, requestTag, nowMs );
        work->_token                                     = token;
        work->_bUpdateRecommended                        = buildGrant._bUpdateRecommended;
        work->_storeURL                                  = buildGrant._storeURL;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::validateSession( const LoginSessionToken& token, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Validate, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::logout( const LoginSessionToken& token, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Logout, requestTag, nowMs );
        work->_token                                     = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::issueGameTicket( const LoginSessionToken& token, const hashed_string& serverId, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::IssueGameTicket, requestTag, nowMs );
        work->_token    = token;
        work->_serverId = serverId;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::revokeAccountSessions( uint64 accountId, LoginRevokeReason reason, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Revoke, requestTag, nowMs );
        work->_accountId                                 = accountId;
        work->_revokeReason                              = reason;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::linkCredential( const LoginSessionToken& token, const LoginCredential& credential, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::LinkCredential, requestTag, nowMs );
        work->_token      = token;
        work->_credential = credential;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::linkPlatform( const LoginSessionToken& token, string_view provider, const vector<uint8>& ticketBytes, int64 nowMs, uint64 requestTag )
    {
        beginVerification( LoginOperation::LinkPlatform, provider, ticketBytes, token, LoginGrant{}, nowMs, requestTag );
    }

    void LoginService::unlinkPlatform( const LoginSessionToken& token, string_view provider, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::UnlinkPlatform, requestTag, nowMs );
        work->_token    = token;
        work->_provider = string( provider );
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::listLinks( const LoginSessionToken& token, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::ListLinks, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::requestDeletion( const LoginSessionToken& token, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::RequestDeletion, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::cancelDeletion( const LoginSessionToken& token, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::CancelDeletion, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::purgeDueDeletions( int64 nowMs, int32 maxCount )
    {
        if ( maxCount <= 0 )
            return;
        unique_ptr<LoginServiceInternal::LoginMaintenanceWork> work = make_unique<LoginServiceInternal::LoginMaintenanceWork>( this, _pCrypto, _settings, nowMs );
        work->_purgeCount                                           = maxCount;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::markDisconnected( uint64 sessionId, int64 nowMs )
    {
        unique_ptr<LoginServiceInternal::LoginMaintenanceWork> work = make_unique<LoginServiceInternal::LoginMaintenanceWork>( this, _pCrypto, _settings, nowMs );
        work->_disconnectedSessionId                                = sessionId;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::refreshOnlineSessions( int64 nowMs, int32 maxCount )
    {
        if ( _mapAccountToSession.empty() || maxCount <= 0 )
            return;
        vector<LoginSessionRef> listOnline;
        listOnline.reserve( _mapAccountToSession.size() );
        for ( const auto& [accountId, sessionId] : _mapAccountToSession )
        {
            listOnline.push_back( LoginSessionRef{ accountId, sessionId, LoginRevokeReason::None } );
        }
        std::sort( listOnline.begin(), listOnline.end(), LoginServiceInternal::SessionRefLess{} );
        unique_ptr<LoginServiceInternal::LoginMaintenanceWork> work       = make_unique<LoginServiceInternal::LoginMaintenanceWork>( this, _pCrypto, _settings, nowMs );
        const int32                                            checkCount = std::min( maxCount, static_cast<int32>( listOnline.size() ) );
        for ( int32 checkIndex = 0; checkIndex < checkCount; ++checkIndex )
        {
            work->_listOnline.push_back( listOnline[( _refreshCursor + static_cast<size_t>( checkIndex ) ) % listOnline.size()] );
        }
        _refreshCursor = ( _refreshCursor + static_cast<size_t>( checkCount ) ) % listOnline.size();
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::noteRevokedElsewhere( AccountId accountId, uint64 sessionId, LoginRevokeReason reason )
    {
        const auto onlineIt = _mapAccountToSession.find( accountId );
        if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != sessionId )
            return; // 이 프로세스의 세션이 아니다(이미 새 세션으로 바뀌었다)
        _mapAccountToSession.erase( onlineIt );
        _eventBuffer.push( LoginEvent{ accountId, sessionId, reason, LoginEvent::Kind::Revoked } );
        removeOfflineIdentities();
    }

    bool LoginService::findIdentity( AccountId accountId, AccountIdentity& outIdentity ) const
    {
        const auto identityIt = _mapAccountToIdentity.find( accountId );
        if ( identityIt == _mapAccountToIdentity.end() )
            return false;
        outIdentity = identityIt->second;
        return true;
    }

    bool LoginService::findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const
    {
        const auto nameIt = _mapNameKeyToAccount.find( LoginServiceInternal::makeNameKey( displayName ) );
        return nameIt != _mapNameKeyToAccount.end() && findIdentity( nameIt->second, outIdentity );
    }

    uint64 LoginService::findOnlineSessionId( AccountId accountId ) const
    {
        const auto onlineIt = _mapAccountToSession.find( accountId );
        return onlineIt == _mapAccountToSession.end() ? 0 : onlineIt->second;
    }

    void LoginService::applyCompletion( LoginCompletion&& completion, const LoginStoreOutcome& outcome )
    {
        --_pendingCount;
        for ( const LoginSessionRef& revoked : outcome._listRevoked )
        {
            const auto onlineIt = _mapAccountToSession.find( revoked._accountId );
            if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != revoked._sessionId )
            {
                // 다른 서버(또는 재접속 유예)의 세션이다 — 바인딩이 버스로 알린다.
                _remoteRevokeBuffer.push( LoginEvent{ revoked._accountId, revoked._sessionId, revoked._reason, LoginEvent::Kind::Revoked } );
                continue;
            }
            _mapAccountToSession.erase( onlineIt );
            _eventBuffer.push( LoginEvent{ revoked._accountId, revoked._sessionId, revoked._reason, LoginEvent::Kind::Revoked } );
        }
        for ( const LoginSessionRef& offline : outcome._listOffline )
        {
            const auto onlineIt = _mapAccountToSession.find( offline._accountId );
            if ( onlineIt != _mapAccountToSession.end() && onlineIt->second == offline._sessionId )
                _mapAccountToSession.erase( onlineIt );
        }
        for ( const LoginSessionRef& disconnected : outcome._listDisconnected )
        {
            const auto onlineIt = _mapAccountToSession.find( disconnected._accountId );
            if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != disconnected._sessionId )
                continue;
            _mapAccountToSession.erase( onlineIt );
            _eventBuffer.push( LoginEvent{ disconnected._accountId, disconnected._sessionId, LoginRevokeReason::None, LoginEvent::Kind::Disconnected } );
        }
        for ( const LoginSessionRef& online : outcome._listOnline )
        {
            const auto onlineIt = _mapAccountToSession.find( online._accountId );
            if ( onlineIt != _mapAccountToSession.end() && onlineIt->second != online._sessionId )
                _eventBuffer.push( LoginEvent{ online._accountId, onlineIt->second, LoginRevokeReason::DuplicateLogin, LoginEvent::Kind::Revoked } );
            _mapAccountToSession[online._accountId] = online._sessionId;
        }
        for ( const LoginEvent& event : outcome._listEvent )
        {
            _eventBuffer.push( event );
        }
        // 신원 캐시 — 붙어 있는 계정만.
        const bool bGrantOperation = completion._operation == LoginOperation::Login || completion._operation == LoginOperation::Resume ||
                                     completion._operation == LoginOperation::GuestLogin || completion._operation == LoginOperation::PlatformLogin;
        const bool bLinkOperation = completion._operation == LoginOperation::LinkCredential || completion._operation == LoginOperation::LinkPlatform;
        if ( completion._result == LoginResult::Ok && bGrantOperation && completion._grant._identity._accountId != kInvalidAccountId )
        {
            const AccountIdentity& identity                                                  = completion._grant._identity;
            _mapAccountToIdentity[identity._accountId]                                       = identity;
            _mapNameKeyToAccount[LoginServiceInternal::makeNameKey( identity._displayName )] = identity._accountId;
        }
        if ( completion._result == LoginResult::Ok && bLinkOperation && isAccountOnline( completion._identity._accountId ) )
        {
            // 연동이 표시 이름 · 게스트 깃발을 바꿨다 — 옛 이름 색인을 지우고 새 것으로.
            const AccountIdentity& identity = completion._identity;
            const auto             oldIt    = _mapAccountToIdentity.find( identity._accountId );
            if ( oldIt != _mapAccountToIdentity.end() )
                _mapNameKeyToAccount.erase( LoginServiceInternal::makeNameKey( oldIt->second._displayName ) );
            _mapAccountToIdentity[identity._accountId]                                       = identity;
            _mapNameKeyToAccount[LoginServiceInternal::makeNameKey( identity._displayName )] = identity._accountId;
        }
        removeOfflineIdentities();
        if ( completion._requestTag != 0 )
            _completionBuffer.push( std::move( completion ) );
    }

    bool LoginService::consumeAttempt( uint64 clientKey, int64 nowMs, int64& outRetryAfterMs ) { return _mapClientToAttemptBucket.tryConsume( clientKey, nowMs, outRetryAfterMs ); }

    void LoginService::pushImmediate( LoginOperation operation, uint64 requestTag, LoginResult result, const LoginGrant& grant )
    {
        LoginCompletion completion;
        completion._operation  = operation;
        completion._requestTag = requestTag;
        completion._result     = result;
        completion._grant      = grant;
        _completionBuffer.push( std::move( completion ) );
    }

    LoginResult LoginService::evaluateClientBuild( const AccountClientInfo& clientInfo, LoginGrant& outGrant ) const
    {
        if ( _pRemoteConfig == nullptr )
            return LoginResult::Ok;
        if ( AccountUtil::isValidLowerToken( clientInfo._platform, LoginConstant::kMaxPlatformTextSize ) == false )
            return LoginResult::Ok; // 플랫폼을 모르면 판 확인을 할 수 없다 — 프로토콜 판(Hello)이 따로 막는다
        string minimumBuild;
        string recommendedBuild;
        (void)_pRemoteConfig->findText( LoginServiceInternal::makeConfigKey( LoginServiceInternal::kStoreURLKey, clientInfo._platform ), outGrant._storeURL );
        const bool bMinimum     = _pRemoteConfig->findText( LoginServiceInternal::makeConfigKey( LoginServiceInternal::kMinimumBuildKey, clientInfo._platform ), minimumBuild );
        const bool bRecommended = _pRemoteConfig->findText( LoginServiceInternal::makeConfigKey( LoginServiceInternal::kRecommendedBuildKey, clientInfo._platform ),
                                                            recommendedBuild );
        if ( bMinimum && AccountUtil::compareBuild( clientInfo._build, minimumBuild ) < 0 )
            return LoginResult::UpdateRequired;
        if ( bRecommended && AccountUtil::compareBuild( clientInfo._build, recommendedBuild ) < 0 )
            outGrant._bUpdateRecommended = SW_TRUE;
        else
            outGrant._storeURL.clear();
        return LoginResult::Ok;
    }

    IPlatformLoginProvider* LoginService::findPlatformProvider( string_view provider ) const
    {
        for ( IPlatformLoginProvider* pProvider : _listPlatformProvider )
        {
            if ( provider == pProvider->getName() )
                return pProvider;
        }
        return nullptr;
    }

    void LoginService::beginVerification( LoginOperation operation, string_view provider, const vector<uint8>& ticketBytes, const LoginSessionToken& token,
                                          const LoginGrant& buildGrant, int64 nowMs, uint64 requestTag )
    {
        IPlatformLoginProvider* pProvider = findPlatformProvider( provider );
        if ( pProvider == nullptr )
        {
            pushImmediate( operation, requestTag, LoginResult::ProviderUnavailable, LoginGrant{} );
            return;
        }
        const bool bTicketOk = ticketBytes.empty() == false && ticketBytes.size() <= static_cast<size_t>( LoginConstant::kMaxPlatformTicketSize );
        if ( bTicketOk == false )
        {
            pushImmediate( operation, requestTag, LoginResult::ProviderRejected, LoginGrant{} );
            return;
        }
        PendingVerification& pending = _listPendingVerification.emplace_back();
        pending._token               = token;
        pending._provider            = string( provider );
        pending._storeURL            = buildGrant._storeURL;
        pending._pProvider           = pProvider;
        pending._requestTag          = requestTag;
        pending._nowMs               = nowMs;
        pending._operation           = operation;
        pending._bUpdateRecommended  = buildGrant._bUpdateRecommended;
        pending._verificationId      = pProvider->submitVerification( ticketBytes, nowMs );
    }

    void LoginService::finishVerification( const PendingVerification& pending, const PlatformLoginVerification& verification )
    {
        if ( verification.isVerified() == false )
        {
            const LoginResult result = verification._bUnavailable == SW_TRUE ? LoginResult::ProviderUnavailable : LoginResult::ProviderRejected;
            pushImmediate( pending._operation, pending._requestTag, result, LoginGrant{} );
            return;
        }
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, pending._operation, pending._requestTag, pending._nowMs );
        work->_token              = pending._token;
        work->_provider           = pending._provider;
        work->_subject            = verification._subject;
        work->_displayNameHint    = verification._displayName;
        work->_bUpdateRecommended = pending._bUpdateRecommended;
        work->_storeURL           = pending._storeURL;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::removeOfflineIdentities()
    {
        for ( auto identityIt = _mapAccountToIdentity.begin(); identityIt != _mapAccountToIdentity.end(); )
        {
            if ( _mapAccountToSession.find( identityIt->first ) != _mapAccountToSession.end() )
            {
                ++identityIt;
                continue;
            }
            _mapNameKeyToAccount.erase( LoginServiceInternal::makeNameKey( identityIt->second._displayName ) );
            identityIt = _mapAccountToIdentity.erase( identityIt );
        }
    }
} // namespace sw
