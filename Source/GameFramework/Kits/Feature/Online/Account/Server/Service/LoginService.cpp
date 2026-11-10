#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginSecretVault.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include <algorithm>

namespace sw
{
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
        };
    } // namespace

    /** @brief 로그인 요청 하나의 입력입니다. 저장소 스레드가 읽는 값 복사이고, 돈 뒤 비밀(비밀번호 · 장치 비밀 · 외부 주체)을 지운다. */
    struct LoginCallInput
    {
        LoginCredential   _credential{};
        LoginSessionToken _token{};
        hashed_string     _serverID{};
        string            _provider{};
        string            _subject{};
        string            _displayNameHint{};
        string            _storeURL{};
        vector<uint8>     _listSealedLinkSecret{}; ///< 외부 로그인이 준 연동 비밀(이미 암호화됨). 없으면 비어 있다
        uint64            _accountID{ 0 };
        uint64            _requestTag{ 0 };
        UnixMilliseconds  _nowMs{ 0 };
        LoginOperation    _operation{ LoginOperation::Login };
        LoginRevokeReason _revokeReason{ LoginRevokeReason::Administrative };
        uint8             _arrDeviceSecret[LoginConstant::kDeviceSecretSize]{};
        uint8             _bUpdateRecommended{ SW_FALSE };
    };

    /** @brief 끊김, 세션 다시 읽기, 탈퇴 정리의 입력입니다. 완료가 없고 부작용만 있습니다. */
    struct LoginMaintenanceInput
    {
        vector<LoginSessionRef> _listOnline{};
        uint64                  _disconnectedSessionID{ 0 };
        UnixMilliseconds        _nowMs{ 0 };
        int32                   _purgeCount{ 0 };
    };

    namespace
    {
        /** @brief 로그인 요청 · 정리 일이 완료로 넘기는 값입니다. */

        struct LoginCallResult
        {
            LoginCompletion  _completion{};
            LoginStoreResult _outcome{};
        };

        /** @brief 저장소 스레드에서 도는 로그인 일입니다. 서비스 멤버를 만지지 않고 인자로 받은 값만 씁니다. */
        struct LoginStoreRun
        {
            static LoginCallResult runCall( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings, const LoginTicketAuthority& authority,
                                            LoginCallInput& inoutInput )
            {
                LoginCallInput& input = inoutInput;
                LoginCallResult done;
                done._completion._operation  = input._operation;
                done._completion._requestTag = input._requestTag;
                LoginCompletion& completion  = done._completion;
                LoginStoreLogic  logic{ connection, crypto, settings, authority, done._outcome };
                const int64      nowMs = input._nowMs.count();
                switch ( input._operation )
                {
                    case LoginOperation::Register:
                    {
                        uint64 accountID                  = 0;
                        completion._result                = logic.registerAccount( input._credential, &accountID );
                        completion._identity._accountID   = accountID;
                        completion._identity._displayName = input._credential._loginName;
                        break;
                    }
                    case LoginOperation::Login:
                    {
                        completion._result = logic.login( input._credential, nowMs, completion._grant );
                        break;
                    }
                    case LoginOperation::GuestLogin:
                    {
                        completion._result = logic.guestLogin( input._arrDeviceSecret, nowMs, completion._grant );
                        break;
                    }
                    case LoginOperation::PlatformLogin:
                    {
                        completion._result = logic.platformLogin( input._provider, input._subject, input._displayNameHint, &input._listSealedLinkSecret, nowMs, completion._grant );
                        break;
                    }
                    case LoginOperation::Resume:
                    {
                        completion._result = logic.resumeSession( input._token, nowMs, completion._grant );
                        break;
                    }
                    case LoginOperation::Validate:
                    {
                        completion._result = logic.validateSession( input._token, nowMs, completion._identity, &completion._grant._revokeReason );
                        break;
                    }
                    case LoginOperation::Logout:
                    {
                        completion._result = logic.logout( input._token, nowMs );
                        break;
                    }
                    case LoginOperation::IssueGameTicket:
                    {
                        completion._result = logic.issueGameTicket( input._token, input._serverID, nowMs, completion._ticket );
                        break;
                    }
                    case LoginOperation::Revoke:
                    {
                        completion._result = logic.revokeAccountSessions( input._accountID, input._revokeReason, nowMs );
                        break;
                    }
                    case LoginOperation::LinkCredential:
                    {
                        completion._result = logic.linkCredential( input._token, input._credential, nowMs, completion._identity );
                        break;
                    }
                    case LoginOperation::LinkPlatform:
                    {
                        completion._result = logic.linkPlatform( input._token, input._provider, input._subject, &input._listSealedLinkSecret, nowMs, completion._identity );
                        break;
                    }
                    case LoginOperation::UnlinkPlatform:
                    {
                        completion._result = logic.unlinkPlatform( input._token, input._provider, nowMs );
                        break;
                    }
                    case LoginOperation::ListLinks:
                    {
                        completion._result = logic.listLinks( input._token, nowMs, completion._linkSummary );
                        break;
                    }
                    case LoginOperation::RequestDeletion:
                    {
                        completion._result = logic.requestDeletion( input._token, nowMs, completion._grant._deletionDueMs );
                        break;
                    }
                    case LoginOperation::CancelDeletion:
                    {
                        completion._result = logic.cancelDeletion( input._token, nowMs );
                        break;
                    }
                }
                if ( completion._result == LoginResult::Ok && input._bUpdateRecommended == SW_TRUE )
                {
                    completion._grant._bUpdateRecommended = SW_TRUE;
                    completion._grant._storeURL           = input._storeURL;
                }
                // 워커를 떠나면 비밀번호, 장치 비밀, 외부 주체를 보관하지 않는다. 별도로 지우기(wipe)는 하지 않으며 서버의 관례를 따른다.
                input._credential._password.clear();
                input._subject.clear();
                Memory::wipe( input._arrDeviceSecret, sizeof( input._arrDeviceSecret ) );
                return done;
            }

            static LoginStoreResult runMaintenance( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings, const LoginTicketAuthority& authority,
                                                    const LoginMaintenanceInput& input )
            {
                LoginStoreResult outcome;
                LoginStoreLogic  logic{ connection, crypto, settings, authority, outcome };
                if ( input._disconnectedSessionID != 0 )
                    logic.markDisconnected( input._disconnectedSessionID, input._nowMs.count() );
                if ( input._listOnline.empty() == false )
                    logic.refreshSessions( input._listOnline, input._nowMs.count() );
                if ( input._purgeCount > 0 )
                    (void)logic.purgeDueDeletions( input._nowMs.count(), input._purgeCount );
                return outcome;
            }
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
        , _verifications{}
        , _listVerificationScratch{}
        , _listRevocationScratch{}
        , _revocations{}
        , _mapAccountToSession{}
        , _mapAccountToIdentity{}
        , _mapNameKeyToAccount{}
        , _mapClientToAttemptBucket{}
        , _pCrypto{ nullptr }
        , _pRemoteConfig{ nullptr }
        , _pSecretVault{ nullptr }
        , _refreshCursor{ 0 }
        , _revocationScanJob{}
        , _store{}
    {
    }

    void LoginService::initialize( IServiceStore* pStore, ILoginCrypto* pCrypto, const LoginServiceConfig& settings,
                                   const uint8 ( &arrTicketMasterKey )[LoginTicketAuthority::kMasterKeySize] )
    {
        _store.bindStore( pStore );
        _pCrypto  = pCrypto;
        _settings = settings;
        _revocationScanJob.setPeriod( settings._revocationScanMs );
        _ticketAuthority.initialize( pCrypto, arrTicketMasterKey );
        _mapClientToAttemptBucket.initialize( settings._attemptBurst, settings._attemptRefillMs, kMaxTrackedClientCount );
    }

    void LoginService::shutdown()
    {
        _ticketAuthority.shutdown();
        _mapAccountToSession.clear();
        _mapAccountToIdentity.clear();
        _mapNameKeyToAccount.clear();
        _verifications.cancelAll(); // 답을 기다리던 요청은 버린다 — 연결이 닫히며 호스트가 답 없는 요청을 끝낸다
        _revocations.cancelAll();   // 철회 대기열 항목은 저장소에 남아 다음 기동이 다시 한다
        _registeredPlatformProvider.clear();
        _eventBuffer.clear();
        _remoteRevokeBuffer.clear();
        _store.bindStore( nullptr );
        _pCrypto       = nullptr;
        _pRemoteConfig = nullptr;
        _pSecretVault  = nullptr;
        _revocationScanJob.finish();
    }

    bool LoginService::registerPlatformProvider( IPlatformLoginProvider* pProvider )
    {
        if ( pProvider == nullptr || AccountUtil::isValidLowerToken( pProvider->getName(), LoginConstant::kMaxProviderNameSize ) == false )
        {
            SW_LOG_ERROR_ID( "LoginService.PlatformLoginProviderNo", "platform login provider has no valid name" );
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
                (void)_verifications.answer( ProviderRequestKey{ pProvider, verification._verificationID }, verification ); // 모르는 번호(내린 뒤의 답)는 버린다
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
                (void)_revocations.answer( ProviderRequestKey{ pProvider, revocation._revocationID }, revocation );
            }
        }
        const bool bSecretsReadable = _pSecretVault != nullptr && _pSecretVault->isReady();
        const bool bMayScan         = bAnyRevoker && bSecretsReadable && _revocations.isIdle() && _store.hasStore();
        if ( bMayScan == false || _revocationScanJob.tryStart( nowMs ) == false )
            return;
        const int32 maxCount = _settings._revocationBatchSize;
        _store.submit( [maxCount]( IServiceStoreConnection& connection )
        {
            ServiceStoreRead<vector<LoginRevocationEntry>> read;
            read._result = LoginStoreLogic::listRevocations( connection, maxCount, read._value );
            return read;
        }, [this, nowMs]( ServiceStoreRead<vector<LoginRevocationEntry>>& read )
        { applyRevocationScan( std::move( read._value ), read.isOk(), nowMs ); } );
    }

    void LoginService::applyRevocationScan( vector<LoginRevocationEntry>&& listEntry, bool bReadOk, UnixMilliseconds nowMs )
    {
        _revocationScanJob.finish();
        if ( bReadOk == false || _pSecretVault == nullptr )
            return; // 저장소에 문제가 있으므로 다음 주기에 다시 읽는다
        for ( LoginRevocationEntry& entry : listEntry )
        {
            IPlatformLoginProvider* pProvider = _registeredPlatformProvider.findByName( entry._provider );
            if ( pProvider == nullptr || pProvider->supportsRevocation() == false )
            {
                SW_LOG_WARNING_ID( "LoginService.LoginRevokeQueueHolds", "login revoke queue holds a secret of provider '%#' that is not configured here - it stays in the queue", entry._provider.c_str() );
                continue;
            }
            string secret;
            if ( _pSecretVault->open( entry._sealedSecret, entry._provider, secret ) == false )
            {
                SW_LOG_ERROR_ID( "LoginService.LoginRevokeQueueEntry", "login revoke queue entry '%#' cannot be opened (wrong secret key?) - it stays in the queue", entry._key.c_str() );
                continue;
            }
            const uint64 revocationID = pProvider->submitRevocation( secret, nowMs );
            StringUtil::wipeSecret( secret ); // 풀어 낸 비밀을 남기지 않는다
            (void)_revocations.track( ProviderRequestKey{ pProvider, revocationID }, 0, [this, key = std::move( entry._key )]( const PlatformLoginRevocation& revocation )
            {
                // 제공자에 닿지 못했으면 대기열에 남아 다음 읽기가 다시 한다. 지우지 못하면 다음 읽기가 같은 철회를 다시 한다(제공자는 이미 무효인 토큰을 받아들인다).
                if ( revocation._bUnavailable == SW_FALSE && _store.hasStore() )
                {
                    _store.submit( [key]( IServiceStoreConnection& connection )
                    { (void)LoginStoreLogic::eraseRevocation( connection, key ); },
                                   []() {} );
                }
            } );
        }
    }

    void LoginService::registerAccount( const LoginCredential& credential, uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginGrant limited; // 가입도 비싼 비밀번호 해시를 돌린다 - 로그인과 같은 버킷으로 막지 않으면 해시로 서버를 묶을 수 있다
        if ( consumeAttempt( clientKey, nowMs, limited._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::Register, requestTag, LoginOutcome::fail( OnlineError::kRateLimited ), limited );
            return;
        }
        seedAccount( credential, nowMs, requestTag );
    }

    void LoginService::seedAccount( const LoginCredential& credential, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::Register, requestTag, nowMs );
        input._credential    = credential;
        submitCall( std::move( input ) );
    }

    void LoginService::login( const LoginCredential& credential, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginGrant         buildGrant;
        const LoginOutcome build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::Login, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::Login, requestTag, LoginOutcome::fail( OnlineError::kRateLimited ), buildGrant );
            return;
        }
        LoginCallInput input      = makeCallInput( LoginOperation::Login, requestTag, nowMs );
        input._credential         = credential;
        input._bUpdateRecommended = buildGrant._bUpdateRecommended;
        input._storeURL           = buildGrant._storeURL;
        submitCall( std::move( input ) );
    }

    void LoginService::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs,
                                   uint64 requestTag )
    {
        LoginGrant         buildGrant;
        const LoginOutcome build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::GuestLogin, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::GuestLogin, requestTag, LoginOutcome::fail( OnlineError::kRateLimited ), buildGrant );
            return;
        }
        LoginCallInput input = makeCallInput( LoginOperation::GuestLogin, requestTag, nowMs );
        Memory::copy( input._arrDeviceSecret, arrDeviceSecret, LoginConstant::kDeviceSecretSize );
        input._bUpdateRecommended = buildGrant._bUpdateRecommended;
        input._storeURL           = buildGrant._storeURL;
        submitCall( std::move( input ) );
    }

    void LoginService::platformLogin( string_view provider, const vector<uint8>& ticketBytes, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs,
                                      uint64 requestTag )
    {
        LoginGrant         buildGrant;
        const LoginOutcome build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::PlatformLogin, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::PlatformLogin, requestTag, LoginOutcome::fail( OnlineError::kRateLimited ), buildGrant );
            return;
        }
        beginVerification( LoginOperation::PlatformLogin, provider, ticketBytes, LoginSessionToken{}, buildGrant, nowMs, requestTag );
    }

    void LoginService::resumeSession( const LoginSessionToken& token, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginGrant         buildGrant;
        const LoginOutcome build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::Resume, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::Resume, requestTag, LoginOutcome::fail( OnlineError::kRateLimited ), buildGrant );
            return;
        }
        LoginCallInput input      = makeCallInput( LoginOperation::Resume, requestTag, nowMs );
        input._token              = token;
        input._bUpdateRecommended = buildGrant._bUpdateRecommended;
        input._storeURL           = buildGrant._storeURL;
        submitCall( std::move( input ) );
    }

    void LoginService::validateSession( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::Validate, requestTag, nowMs );
        input._token         = token;
        submitCall( std::move( input ) );
    }

    void LoginService::logout( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::Logout, requestTag, nowMs );
        input._token         = token;
        submitCall( std::move( input ) );
    }

    void LoginService::issueGameTicket( const LoginSessionToken& token, const hashed_string& serverID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::IssueGameTicket, requestTag, nowMs );
        input._token         = token;
        input._serverID      = serverID;
        submitCall( std::move( input ) );
    }

    void LoginService::revokeAccountSessions( uint64 accountID, LoginRevokeReason reason, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::Revoke, requestTag, nowMs );
        input._accountID     = accountID;
        input._revokeReason  = reason;
        submitCall( std::move( input ) );
    }

    void LoginService::linkCredential( const LoginSessionToken& token, const LoginCredential& credential, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::LinkCredential, requestTag, nowMs );
        input._token         = token;
        input._credential    = credential;
        submitCall( std::move( input ) );
    }

    void LoginService::linkPlatform( const LoginSessionToken& token, string_view provider, const vector<uint8>& ticketBytes, UnixMilliseconds nowMs, uint64 requestTag )
    {
        beginVerification( LoginOperation::LinkPlatform, provider, ticketBytes, token, LoginGrant{}, nowMs, requestTag );
    }

    void LoginService::unlinkPlatform( const LoginSessionToken& token, string_view provider, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::UnlinkPlatform, requestTag, nowMs );
        input._token         = token;
        input._provider      = string( provider );
        submitCall( std::move( input ) );
    }

    void LoginService::listLinks( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::ListLinks, requestTag, nowMs );
        input._token         = token;
        submitCall( std::move( input ) );
    }

    void LoginService::requestDeletion( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::RequestDeletion, requestTag, nowMs );
        input._token         = token;
        submitCall( std::move( input ) );
    }

    void LoginService::cancelDeletion( const LoginSessionToken& token, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::CancelDeletion, requestTag, nowMs );
        input._token         = token;
        submitCall( std::move( input ) );
    }

    void LoginService::purgeDueDeletions( UnixMilliseconds nowMs, int32 maxCount )
    {
        if ( maxCount <= 0 )
            return;
        LoginMaintenanceInput input;
        input._nowMs      = nowMs;
        input._purgeCount = maxCount;
        submitMaintenance( std::move( input ) );
    }

    void LoginService::markDisconnected( uint64 sessionID, UnixMilliseconds nowMs )
    {
        LoginMaintenanceInput input;
        input._nowMs                 = nowMs;
        input._disconnectedSessionID = sessionID;
        submitMaintenance( std::move( input ) );
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
        VectorUtil::sort( listOnline, LoginServiceInternal::SessionRefLess{} );
        LoginMaintenanceInput input;
        input._nowMs           = nowMs;
        const int32 checkCount = MathUtil::min( maxCount, static_cast<int32>( listOnline.size() ) );
        for ( int32 checkIndex = 0; checkIndex < checkCount; ++checkIndex )
        {
            input._listOnline.push_back( listOnline[( _refreshCursor + static_cast<size_t>( checkIndex ) ) % listOnline.size()] );
        }
        _refreshCursor = ( _refreshCursor + static_cast<size_t>( checkCount ) ) % listOnline.size();
        submitMaintenance( std::move( input ) );
    }

    void LoginService::recordRevokedElsewhere( AccountID accountID, uint64 sessionID, LoginRevokeReason reason )
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

    void LoginService::submitCall( LoginCallInput&& input )
    {
        ILoginCrypto*               pCrypto    = _pCrypto;
        const LoginTicketAuthority* pAuthority = &_ticketAuthority; // 저장소 스레드는 읽기만 한다
        _store.submit( [pCrypto, pAuthority, settings = _settings, input = std::move( input )]( IServiceStoreConnection& connection ) mutable
        { return LoginStoreRun::runCall( connection, *pCrypto, settings, *pAuthority, input ); },
                       [this]( LoginCallResult& done )
        { applyCompletion( std::move( done._completion ), done._outcome ); } );
    }

    void LoginService::submitMaintenance( LoginMaintenanceInput&& input )
    {
        ILoginCrypto*               pCrypto    = _pCrypto;
        const LoginTicketAuthority* pAuthority = &_ticketAuthority;
        _store.submit( [pCrypto, pAuthority, settings = _settings, input = std::move( input )]( IServiceStoreConnection& connection )
        { return LoginStoreRun::runMaintenance( connection, *pCrypto, settings, *pAuthority, input ); },
                       [this]( const LoginStoreResult& outcome )
        {
            LoginCompletion none;
            none._requestTag = 0; // 요청 태그 0은 완료 목록에 쌓지 않는다(applyCompletion)
            applyCompletion( std::move( none ), outcome );
        } );
    }

    LoginCallInput LoginService::makeCallInput( LoginOperation operation, uint64 requestTag, UnixMilliseconds nowMs )
    {
        LoginCallInput input;
        input._operation  = operation;
        input._requestTag = requestTag;
        input._nowMs      = nowMs;
        return input;
    }

    void LoginService::applyCompletion( LoginCompletion&& completion, const LoginStoreResult& outcome )
    {
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

    void LoginService::pushImmediate( LoginOperation operation, uint64 requestTag, LoginOutcome result, const LoginGrant& grant )
    {
        LoginCompletion completion;
        completion._operation  = operation;
        completion._requestTag = requestTag;
        completion._result     = result;
        completion._grant      = grant;
        _completionBuffer.push( std::move( completion ) );
    }

    LoginOutcome LoginService::evaluateClientBuild( const AccountClientInfo& clientInfo, LoginGrant& outGrant ) const
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
            return LoginOutcome::fail( OnlineError::kUpdateRequired );
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
        PendingVerification pending;
        pending._token              = token;
        pending._provider           = string( provider );
        pending._storeURL           = buildGrant._storeURL;
        pending._requestTag         = requestTag;
        pending._nowMs              = nowMs;
        pending._operation          = operation;
        pending._bUpdateRecommended = buildGrant._bUpdateRecommended;
        const ProviderRequestKey key{ pProvider, pProvider->submitVerification( ticketBytes, nowMs ) };
        if ( _verifications.track( key, 0, [this, pending = std::move( pending )]( const PlatformLoginVerification& verification )
        { finishVerification( pending, verification ); } ) == false )
            pushImmediate( operation, requestTag, LoginResult::ProviderUnavailable, LoginGrant{} ); // 제공자가 같은 확인 번호를 두 번 냈다
    }

    void LoginService::finishVerification( const PendingVerification& pending, const PlatformLoginVerification& verification )
    {
        if ( verification.isVerified() == false )
        {
            const LoginOutcome result = verification._bUnavailable == SW_TRUE ? LoginResult::ProviderUnavailable : LoginResult::ProviderRejected;
            pushImmediate( pending._operation, pending._requestTag, result, LoginGrant{} );
            return;
        }
        LoginCallInput input      = makeCallInput( pending._operation, pending._requestTag, pending._nowMs );
        input._token              = pending._token;
        input._provider           = pending._provider;
        input._subject            = verification._subject;
        input._displayNameHint    = verification._displayName;
        input._bUpdateRecommended = pending._bUpdateRecommended;
        input._storeURL           = pending._storeURL;
        if ( verification._linkSecret.empty() == false )
        {
            // 연동 비밀은 암호화해서만 저장한다. 도구가 없거나 암호화에 실패하면 비밀을 버린다(로그인은 성공이다 — 탈퇴 때 철회할 것이 없을 뿐이다).
            const bool bSealed = _pSecretVault != nullptr && _pSecretVault->isReady() && _pSecretVault->seal( verification._linkSecret, pending._provider, input._listSealedLinkSecret );
            if ( bSealed == false )
            {
                input._listSealedLinkSecret.clear();
                SW_LOG_ERROR_ID( "LoginService.LoginProviderGaveLink", "login provider '%#' gave a link secret but the secret vault is not available - it is not stored", pending._provider.c_str() );
            }
        }
        submitCall( std::move( input ) );
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
