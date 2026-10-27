#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"

#include "Core/Container/StringUtil.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginSecretVault.h"

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

            /** @brief 돌아가며 다시 읽는 차례가 해시 맵의 순서에 기대지 않게(결정적이게) 계정 id 로 정렬합니다. */
            struct SessionRefLess
            {
                bool operator()( const LoginSessionRef& left, const LoginSessionRef& right ) const { return left._accountID < right._accountID; }
            };

            static string makeNameKey( string_view displayName ) { return StringUtil::toLower( string( displayName ).c_str() ); }

            static string makeConfigKey( const utf8* pPrefix, string_view platform )
            {
                string key{ pPrefix };
                key += platform;
                return key;
            }

            /** @brief 로그인 요청 하나를 저장소 스레드에서 실행하고 결과를 서비스에 돌려줍니다. 입력은 모두 복사해 보관합니다. */
            class LoginWork final : public IServiceStoreWork
            {
            public:
                LoginCredential   _credential;
                LoginSessionToken _token;
                hashed_string     _serverID;
                string            _provider;
                string            _subject;
                string            _displayNameHint;
                string            _storeURL;
                vector<uint8>     _listSealedLinkSecret; ///< 외부 로그인이 준 연동 비밀(이미 암호화됨). 없으면 비어 있다
                uint64            _accountID;
                LoginRevokeReason _revokeReason;
                uint8             _arrDeviceSecret[LoginConstant::kDeviceSecretSize];
                uint8             _bUpdateRecommended;

                LoginWork( LoginService* pService, ILoginCrypto* pCrypto, const LoginServiceConfig& settings, LoginOperation operation, uint64 requestTag, UnixMilliseconds nowMs )
                    : _credential{}
                    , _token{}
                    , _serverID{}
                    , _provider{}
                    , _subject{}
                    , _displayNameHint{}
                    , _storeURL{}
                    , _listSealedLinkSecret{}
                    , _accountID{ 0 }
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
                            uint64 accountID                   = 0;
                            _completion._result                = logic.registerAccount( _credential, &accountID );
                            _completion._identity._accountID   = accountID;
                            _completion._identity._displayName = _credential._loginName;
                            break;
                        }
                        case LoginOperation::Login:
                        {
                            _completion._result = logic.login( _credential, _nowMs.count(), _completion._grant );
                            break;
                        }
                        case LoginOperation::GuestLogin:
                        {
                            _completion._result = logic.guestLogin( _arrDeviceSecret, _nowMs.count(), _completion._grant );
                            break;
                        }
                        case LoginOperation::PlatformLogin:
                        {
                            _completion._result = logic.platformLogin( _provider, _subject, _displayNameHint, &_listSealedLinkSecret, _nowMs.count(), _completion._grant );
                            break;
                        }
                        case LoginOperation::Resume:
                        {
                            _completion._result = logic.resumeSession( _token, _nowMs.count(), _completion._grant );
                            break;
                        }
                        case LoginOperation::Validate:
                        {
                            _completion._result = logic.validateSession( _token, _nowMs.count(), _completion._identity, &_completion._grant._revokeReason );
                            break;
                        }
                        case LoginOperation::Logout:
                        {
                            _completion._result = logic.logout( _token, _nowMs.count() );
                            break;
                        }
                        case LoginOperation::IssueGameTicket:
                        {
                            _completion._result = logic.issueGameTicket( _token, _serverID, _nowMs.count(), _completion._ticket );
                            break;
                        }
                        case LoginOperation::Revoke:
                        {
                            _completion._result = logic.revokeAccountSessions( _accountID, _revokeReason, _nowMs.count() );
                            break;
                        }
                        case LoginOperation::LinkCredential:
                        {
                            _completion._result = logic.linkCredential( _token, _credential, _nowMs.count(), _completion._identity );
                            break;
                        }
                        case LoginOperation::LinkPlatform:
                        {
                            _completion._result = logic.linkPlatform( _token, _provider, _subject, &_listSealedLinkSecret, _nowMs.count(), _completion._identity );
                            break;
                        }
                        case LoginOperation::UnlinkPlatform:
                        {
                            _completion._result = logic.unlinkPlatform( _token, _provider, _nowMs.count() );
                            break;
                        }
                        case LoginOperation::ListLinks:
                        {
                            _completion._result = logic.listLinks( _token, _nowMs.count(), _completion._linkSummary );
                            break;
                        }
                        case LoginOperation::RequestDeletion:
                        {
                            _completion._result = logic.requestDeletion( _token, _nowMs.count(), _completion._grant._deletionDueMs );
                            break;
                        }
                        case LoginOperation::CancelDeletion:
                        {
                            _completion._result = logic.cancelDeletion( _token, _nowMs.count() );
                            break;
                        }
                    }
                    if ( _completion._result == LoginResult::Ok && _bUpdateRecommended == SW_TRUE )
                    {
                        _completion._grant._bUpdateRecommended = SW_TRUE;
                        _completion._grant._storeURL           = _storeURL;
                    }
                    // 워커를 떠나면 비밀번호, 장치 비밀, 외부 주체를 보관하지 않는다. 별도로 지우기(wipe)는 하지 않으며 서버의 관례를 따른다.
                    _credential._password.clear();
                    _subject.clear();
                    Memory::set( _arrDeviceSecret, 0, sizeof( _arrDeviceSecret ) );
                }

                void complete() override { _pService->applyCompletion( std::move( _completion ), _outcome ); }

            private:
                LoginStoreOutcome  _outcome;
                LoginCompletion    _completion;
                LoginServiceConfig _settings; ///< 복사한 값이다. 서비스 설정이 바뀌어도 이 작업은 맡길 때의 값으로 실행한다
                LoginService*      _pService;
                ILoginCrypto*      _pCrypto;
                UnixMilliseconds   _nowMs;
            };

            /** @brief 탈퇴한 계정의 연동 비밀 철회 대기열을 읽는 작업입니다. 읽은 항목은 서비스가 비밀을 풀어 제공자에 철회를 맡깁니다. */
            class LoginRevocationScanWork final : public IServiceStoreWork
            {
            public:
                LoginRevocationScanWork( LoginService* pService, int32 maxCount, UnixMilliseconds nowMs )
                    : _listEntry{}
                    , _pService{ pService }
                    , _maxCount{ maxCount }
                    , _nowMs{ nowMs }
                    , _bReadOk{ SW_FALSE }
                {
                }

                void run( IServiceStoreConnection& connection ) override
                {
                    _bReadOk = LoginStoreLogic::listRevocations( connection, _maxCount, _listEntry ) == ServiceStoreResult::Ok ? SW_TRUE : SW_FALSE;
                }

                void complete() override { _pService->applyRevocationScan( std::move( _listEntry ), _bReadOk == SW_TRUE, _nowMs ); }

            private:
                vector<LoginRevocationEntry> _listEntry;
                LoginService*                _pService;
                int32                        _maxCount;
                UnixMilliseconds             _nowMs;
                uint8                        _bReadOk;
            };

            /** @brief 철회가 끝난 대기열 항목 하나를 지우는 작업입니다. 지우지 못하면 다음 읽기가 같은 철회를 다시 한다(제공자는 이미 무효인 토큰을 받아들인다). */
            class LoginRevocationEraseWork final : public IServiceStoreWork
            {
            public:
                LoginRevocationEraseWork( LoginService* pService, string key )
                    : _key{ std::move( key ) }
                    , _pService{ pService }
                {
                }

                void run( IServiceStoreConnection& connection ) override { (void)LoginStoreLogic::eraseRevocation( connection, _key ); }
                void complete() override { _pService->noteStoreWorkDone(); }

            private:
                string        _key;
                LoginService* _pService;
            };

            /** @brief 끊김, 세션 다시 읽기, 탈퇴 정리 작업입니다. 완료가 없고 부작용만 있습니다. */
            class LoginMaintenanceWork final : public IServiceStoreWork
            {
            public:
                vector<LoginSessionRef> _listOnline;
                uint64                  _disconnectedSessionID;
                int32                   _purgeCount;

                LoginMaintenanceWork( LoginService* pService, ILoginCrypto* pCrypto, const LoginServiceConfig& settings, UnixMilliseconds nowMs )
                    : _listOnline{}
                    , _disconnectedSessionID{ 0 }
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
                    if ( _disconnectedSessionID != 0 )
                        logic.markDisconnected( _disconnectedSessionID, _nowMs.count() );
                    if ( _listOnline.empty() == false )
                        logic.refreshSessions( _listOnline, _nowMs.count() );
                    if ( _purgeCount > 0 )
                        (void)logic.purgeDueDeletions( _nowMs.count(), _purgeCount );
                }

                void complete() override
                {
                    LoginCompletion none;
                    none._requestTag = 0; // 요청 태그 0은 완료 목록에 쌓지 않는다(applyCompletion)
                    _pService->applyCompletion( std::move( none ), _outcome );
                }

            private:
                LoginStoreOutcome  _outcome;
                LoginServiceConfig _settings;
                LoginService*      _pService;
                ILoginCrypto*      _pCrypto;
                UnixMilliseconds   _nowMs;
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
        , _registeredPlatformProvider{ "LoginServicePlatformProvider", RegistrationOrder::Insertion, true }
        , _listPendingVerification{}
        , _listVerificationScratch{}
        , _listRevocationScratch{}
        , _listPendingRevocation{}
        , _mapAccountToSession{}
        , _mapAccountToIdentity{}
        , _mapNameKeyToAccount{}
        , _mapClientToAttemptBucket{}
        , _pStore{ nullptr }
        , _pCrypto{ nullptr }
        , _pRemoteConfig{ nullptr }
        , _pSecretVault{ nullptr }
        , _nextRevocationScanMs{ 0 }
        , _refreshCursor{ 0 }
        , _pendingCount{ 0 }
        , _bRevocationScanInFlight{ SW_FALSE }
    {
    }

    void LoginService::initialize( IServiceStore* pStore, ILoginCrypto* pCrypto, const LoginServiceConfig& settings,
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
        _listPendingRevocation.clear();
        _registeredPlatformProvider.clear();
        _eventBuffer.clear();
        _remoteRevokeBuffer.clear();
        _pStore                  = nullptr;
        _pCrypto                 = nullptr;
        _pRemoteConfig           = nullptr;
        _pSecretVault            = nullptr;
        _bRevocationScanInFlight = SW_FALSE;
    }

    bool LoginService::registerPlatformProvider( IPlatformLoginProvider* pProvider )
    {
        if ( pProvider == nullptr || AccountUtil::isValidLowerToken( pProvider->getName(), LoginConstant::kMaxProviderNameSize ) == false )
        {
            SW_LOG_ERROR( "platform login provider has no valid name" );
            return false;
        }
        return _registeredPlatformProvider.addLogged( pProvider, "Platform login provider", pProvider->getName() ) == RegistrationResult::Added;
    }

    void LoginService::update( UnixMilliseconds nowMs )
    {
        for ( IPlatformLoginProvider* pProvider : _registeredPlatformProvider.getItems() )
        {
            pProvider->update( nowMs );
            _listVerificationScratch.clear();
            (void)pProvider->pollVerifications( _listVerificationScratch );
            for ( const PlatformLoginVerification& verification : _listVerificationScratch )
            {
                for ( size_t pendingIndex = 0; pendingIndex < _listPendingVerification.size(); ++pendingIndex )
                {
                    const PendingVerification& pending  = _listPendingVerification[pendingIndex];
                    const bool                 bMatches = pending._pProvider == pProvider && pending._verificationID == verification._verificationID;
                    if ( bMatches == false )
                        continue;
                    const PendingVerification finished = pending;
                    _listPendingVerification.erase( _listPendingVerification.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
                    finishVerification( finished, verification );
                    break;
                }
            }
        }
        updateRevocations( nowMs );
    }

    void LoginService::updateRevocations( UnixMilliseconds nowMs )
    {
        bool bAnyRevoker = false;
        for ( IPlatformLoginProvider* pProvider : _registeredPlatformProvider.getItems() )
        {
            if ( pProvider->supportsRevocation() == false )
                continue;
            bAnyRevoker = true;
            _listRevocationScratch.clear();
            (void)pProvider->pollRevocations( _listRevocationScratch );
            for ( const PlatformLoginRevocation& revocation : _listRevocationScratch )
            {
                for ( size_t pendingIndex = 0; pendingIndex < _listPendingRevocation.size(); ++pendingIndex )
                {
                    const PendingRevocation& pending = _listPendingRevocation[pendingIndex];
                    if ( pending._pProvider != pProvider || pending._revocationID != revocation._revocationID )
                        continue;
                    if ( revocation._bUnavailable == SW_FALSE && _pStore != nullptr )
                    {
                        ++_pendingCount;
                        _pStore->submit( make_unique<LoginServiceInternal::LoginRevocationEraseWork>( this, pending._queueKey ) );
                    }
                    _listPendingRevocation.erase( _listPendingRevocation.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
                    break; // 제공자에 닿지 못했으면 대기열에 남아 다음 읽기가 다시 한다
                }
            }
        }
        const bool bSecretsReadable = _pSecretVault != nullptr && _pSecretVault->isReady();
        const bool bMayScan         = bAnyRevoker && bSecretsReadable && _bRevocationScanInFlight == SW_FALSE && _listPendingRevocation.empty() && _pStore != nullptr;
        if ( bMayScan == false || nowMs < _nextRevocationScanMs )
            return;
        _nextRevocationScanMs    = nowMs + _settings._revocationScanMs;
        _bRevocationScanInFlight = SW_TRUE;
        ++_pendingCount;
        _pStore->submit( make_unique<LoginServiceInternal::LoginRevocationScanWork>( this, _settings._revocationBatchSize, nowMs ) );
    }

    void LoginService::applyRevocationScan( vector<LoginRevocationEntry>&& listEntry, bool bReadOk, UnixMilliseconds nowMs )
    {
        --_pendingCount;
        _bRevocationScanInFlight = SW_FALSE;
        if ( bReadOk == false || _pSecretVault == nullptr )
            return; // 저장소에 문제가 있으므로 다음 주기에 다시 읽는다
        for ( LoginRevocationEntry& entry : listEntry )
        {
            IPlatformLoginProvider* pProvider = _registeredPlatformProvider.findByName( entry._provider );
            if ( pProvider == nullptr || pProvider->supportsRevocation() == false )
            {
                SW_LOG_WARNING( "login revoke queue holds a secret of provider '%#' that is not configured here - it stays in the queue", entry._provider.c_str() );
                continue;
            }
            string secret;
            if ( _pSecretVault->open( entry._sealedSecret, entry._provider, secret ) == false )
            {
                SW_LOG_ERROR( "login revoke queue entry '%#' cannot be opened (wrong secret key?) - it stays in the queue", entry._key.c_str() );
                continue;
            }
            PendingRevocation& pending = _listPendingRevocation.emplace_back();
            pending._queueKey          = std::move( entry._key );
            pending._pProvider         = pProvider;
            pending._revocationID      = pProvider->submitRevocation( secret, nowMs );
            Memory::set( secret.data(), 0, secret.size() ); // 풀어 낸 비밀을 남기지 않는다
        }
    }

    void LoginService::registerAccount( const LoginCredential& credential, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Register, requestTag, nowMs );
        work->_credential                                = credential;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::login( const LoginCredential& credential, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
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

    void LoginService::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs,
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

    void LoginService::platformLogin( string_view provider, const vector<uint8>& ticketBytes, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs,
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

    void LoginService::resumeSession( const LoginSessionToken& token, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
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

    void LoginService::validateSession( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Validate, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::logout( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Logout, requestTag, nowMs );
        work->_token                                     = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::issueGameTicket( const LoginSessionToken& token, const hashed_string& serverID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::IssueGameTicket, requestTag, nowMs );
        work->_token    = token;
        work->_serverID = serverID;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::revokeAccountSessions( uint64 accountID, LoginRevokeReason reason, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Revoke, requestTag, nowMs );
        work->_accountID                                 = accountID;
        work->_revokeReason                              = reason;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::linkCredential( const LoginSessionToken& token, const LoginCredential& credential, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::LinkCredential, requestTag, nowMs );
        work->_token      = token;
        work->_credential = credential;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::linkPlatform( const LoginSessionToken& token, string_view provider, const vector<uint8>& ticketBytes, UnixMilliseconds nowMs, uint64 requestTag )
    {
        beginVerification( LoginOperation::LinkPlatform, provider, ticketBytes, token, LoginGrant{}, nowMs, requestTag );
    }

    void LoginService::unlinkPlatform( const LoginSessionToken& token, string_view provider, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::UnlinkPlatform, requestTag, nowMs );
        work->_token    = token;
        work->_provider = string( provider );
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::listLinks( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::ListLinks, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::requestDeletion( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::RequestDeletion, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::cancelDeletion( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::CancelDeletion, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::purgeDueDeletions( UnixMilliseconds nowMs, int32 maxCount )
    {
        if ( maxCount <= 0 )
            return;
        unique_ptr<LoginServiceInternal::LoginMaintenanceWork> work = make_unique<LoginServiceInternal::LoginMaintenanceWork>( this, _pCrypto, _settings, nowMs );
        work->_purgeCount                                           = maxCount;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::markDisconnected( uint64 sessionID, UnixMilliseconds nowMs )
    {
        unique_ptr<LoginServiceInternal::LoginMaintenanceWork> work = make_unique<LoginServiceInternal::LoginMaintenanceWork>( this, _pCrypto, _settings, nowMs );
        work->_disconnectedSessionID                                = sessionID;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::refreshOnlineSessions( UnixMilliseconds nowMs, int32 maxCount )
    {
        if ( _mapAccountToSession.empty() || maxCount <= 0 )
            return;
        vector<LoginSessionRef> listOnline;
        listOnline.reserve( _mapAccountToSession.size() );
        for ( const auto& [accountID, sessionID] : _mapAccountToSession )
        {
            listOnline.push_back( LoginSessionRef{ accountID, sessionID, LoginRevokeReason::None } );
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

    void LoginService::noteRevokedElsewhere( AccountID accountID, uint64 sessionID, LoginRevokeReason reason )
    {
        const auto onlineIt = _mapAccountToSession.find( accountID );
        if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != sessionID )
            return; // 이 프로세스의 세션이 아니다. 이미 새 세션으로 바뀌었다
        _mapAccountToSession.erase( onlineIt );
        _eventBuffer.push( LoginEvent{ accountID, sessionID, reason, LoginEvent::Kind::Revoked } );
        removeOfflineIdentities();
    }

    bool LoginService::findIdentity( AccountID accountID, AccountIdentity& outIdentity ) const
    {
        const auto identityIt = _mapAccountToIdentity.find( accountID );
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

    uint64 LoginService::findOnlineSessionID( AccountID accountID ) const
    {
        const auto onlineIt = _mapAccountToSession.find( accountID );
        return onlineIt == _mapAccountToSession.end() ? 0 : onlineIt->second;
    }

    void LoginService::applyCompletion( LoginCompletion&& completion, const LoginStoreOutcome& outcome )
    {
        --_pendingCount;
        for ( const LoginSessionRef& revoked : outcome._listRevoked )
        {
            const auto onlineIt = _mapAccountToSession.find( revoked._accountID );
            if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != revoked._sessionID )
            {
                // 다른 서버(또는 재접속 유예 중)의 세션이므로 바인딩이 버스로 알린다.
                _remoteRevokeBuffer.push( LoginEvent{ revoked._accountID, revoked._sessionID, revoked._reason, LoginEvent::Kind::Revoked } );
                continue;
            }
            _mapAccountToSession.erase( onlineIt );
            _eventBuffer.push( LoginEvent{ revoked._accountID, revoked._sessionID, revoked._reason, LoginEvent::Kind::Revoked } );
        }
        for ( const LoginSessionRef& offline : outcome._listOffline )
        {
            const auto onlineIt = _mapAccountToSession.find( offline._accountID );
            if ( onlineIt != _mapAccountToSession.end() && onlineIt->second == offline._sessionID )
                _mapAccountToSession.erase( onlineIt );
        }
        for ( const LoginSessionRef& disconnected : outcome._listDisconnected )
        {
            const auto onlineIt = _mapAccountToSession.find( disconnected._accountID );
            if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != disconnected._sessionID )
                continue;
            _mapAccountToSession.erase( onlineIt );
            _eventBuffer.push( LoginEvent{ disconnected._accountID, disconnected._sessionID, LoginRevokeReason::None, LoginEvent::Kind::Disconnected } );
        }
        for ( const LoginSessionRef& online : outcome._listOnline )
        {
            const auto onlineIt = _mapAccountToSession.find( online._accountID );
            if ( onlineIt != _mapAccountToSession.end() && onlineIt->second != online._sessionID )
                _eventBuffer.push( LoginEvent{ online._accountID, onlineIt->second, LoginRevokeReason::DuplicateLogin, LoginEvent::Kind::Revoked } );
            _mapAccountToSession[online._accountID] = online._sessionID;
        }
        for ( const LoginEvent& event : outcome._listEvent )
        {
            _eventBuffer.push( event );
        }
        // 신원 캐시는 접속해 있는 계정만 가진다.
        const bool bGrantOperation = completion._operation == LoginOperation::Login || completion._operation == LoginOperation::Resume ||
                                     completion._operation == LoginOperation::GuestLogin || completion._operation == LoginOperation::PlatformLogin;
        const bool bLinkOperation = completion._operation == LoginOperation::LinkCredential || completion._operation == LoginOperation::LinkPlatform;
        const bool bRegisterGrant = completion._result == LoginResult::Ok && bGrantOperation && completion._grant._identity._accountID != kInvalidAccountID;
        if ( bRegisterGrant )
        {
            const AccountIdentity& identity                                                  = completion._grant._identity;
            _mapAccountToIdentity[identity._accountID]                                       = identity;
            _mapNameKeyToAccount[LoginServiceInternal::makeNameKey( identity._displayName )] = identity._accountID;
        }
        const bool bUpdateLinkedIdentity = completion._result == LoginResult::Ok && bLinkOperation && isAccountOnline( completion._identity._accountID );
        if ( bUpdateLinkedIdentity )
        {
            // 연동이 표시 이름과 게스트 플래그를 바꿨으므로 이전 이름 인덱스를 지우고 새 것으로 바꾼다.
            const AccountIdentity& identity = completion._identity;
            const auto             oldIt    = _mapAccountToIdentity.find( identity._accountID );
            if ( oldIt != _mapAccountToIdentity.end() )
                _mapNameKeyToAccount.erase( LoginServiceInternal::makeNameKey( oldIt->second._displayName ) );
            _mapAccountToIdentity[identity._accountID]                                       = identity;
            _mapNameKeyToAccount[LoginServiceInternal::makeNameKey( identity._displayName )] = identity._accountID;
        }
        removeOfflineIdentities();
        if ( completion._requestTag != 0 )
            _completionBuffer.push( std::move( completion ) );
    }

    bool LoginService::consumeAttempt( uint64 clientKey, UnixMilliseconds nowMs, int64& outRetryAfterMs )
    {
        return _mapClientToAttemptBucket.tryConsume( clientKey, nowMs.count(), outRetryAfterMs );
    }

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
            return LoginResult::Ok; // 플랫폼을 모르면 빌드 버전을 확인할 수 없다. 온라인 스키마 해시(Hello)가 따로 막는다
        string minimumBuild;
        string recommendedBuild;
        (void)_pRemoteConfig->findText( LoginServiceInternal::makeConfigKey( LoginServiceInternal::kStoreURLKey, clientInfo._platform ), outGrant._storeURL ); // 선택 값이다 — 없으면 비워 둔다
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

    void LoginService::beginVerification( LoginOperation operation, string_view provider, const vector<uint8>& ticketBytes, const LoginSessionToken& token,
                                          const LoginGrant& buildGrant, UnixMilliseconds nowMs, uint64 requestTag )
    {
        IPlatformLoginProvider* pProvider = _registeredPlatformProvider.findByName( provider );
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
        pending._verificationID      = pProvider->submitVerification( ticketBytes, nowMs );
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
        if ( verification._linkSecret.empty() == false )
        {
            // 연동 비밀은 암호화해서만 저장한다. 도구가 없거나 암호화에 실패하면 비밀을 버린다(로그인은 성공이다 — 탈퇴 때 철회할 것이 없을 뿐이다).
            const bool bSealed = _pSecretVault != nullptr && _pSecretVault->isReady() && _pSecretVault->seal( verification._linkSecret, pending._provider, work->_listSealedLinkSecret );
            if ( bSealed == false )
            {
                work->_listSealedLinkSecret.clear();
                SW_LOG_ERROR( "login provider '%#' gave a link secret but the secret vault is not available - it is not stored", pending._provider.c_str() );
            }
        }
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
