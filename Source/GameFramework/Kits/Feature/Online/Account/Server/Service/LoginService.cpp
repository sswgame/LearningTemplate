#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginSecretVault.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

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
            static constexpr utf8 kAttemptKeyPrefix[]    = "login/attempt/";
            static constexpr utf8 kNonceKeyPrefix[]      = "login/nonce/";

            /** @brief 서버가 낸 모양(소문자 16진 `LoginConstant::kLoginNonceTextSize` 글자)인지입니다. 캐시 키 규칙 밖의 글자가 키에 들어가지 않게 먼저 본다. */
            static bool isValidLoginNonce( string_view nonce )
            {
                if ( nonce.size() != static_cast<size_t>( LoginConstant::kLoginNonceTextSize ) )
                    return false;
                for ( const utf8 character : nonce )
                {
                    const bool bHex = ( character >= '0' && character <= '9' ) || ( character >= 'a' && character <= 'f' );
                    if ( bHex == false )
                        return false;
                }
                return true;
            }

            /** @brief 돌아가며 다시 읽는 차례가 해시 맵의 순서에 기대지 않게(결정적이게) 계정 id 로 정렬합니다. */
            struct SessionRefLess
            {
                bool operator()( const LoginSessionRef& left, const LoginSessionRef& right ) const { return left._accountID < right._accountID; }
            };

            static string makeNameKey( string_view displayName ) { return StringUtil::toLower( displayName ); }

            static string makeConfigKey( const utf8* pPrefix, string_view platform )
            {
                StringBuilder<constant::buffer::kMax128> key;
                key.appendFormat( "%#%#", pPrefix, platform );
                return string( key.view() );
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
        vector<LoginSessionRef>            _listOnline{};
        uint64                             _disconnectedSessionID{ 0 };
        UnixMilliseconds                   _nowMs{ 0 };
        int32                              _purgeCount{ 0 };
        vector<const IAfterDeleteAccount*> _listAfterDeleteAccount{}; ///< 탈퇴 정리가 부를 키트 훅(정리 일에만 싣는다)
    };

    /**
     * @brief 진행 중인 비밀번호 요청 하나입니다(가입 · 로그인 · 이름 연동). 서비스 스레드만 만지고, 단계 사이에 입력 · 해시 줄 자리 · 로그인 문제를 듭니다.
     * @details 비밀번호는 로그인만 끝날 때까지 든다(커밋이 "그사이 바뀌었다" 고 하면 다시 해시한다). 가입 · 연동은 해시를 맡기며 지운다. 저장소 일로는 가지 않는다.
     */
    struct LoginPasswordCall
    {
        LoginCallInput               _input{};
        optional<PasswordHashTicket> _ticket{};    ///< 해시 줄 자리 — 해시를 맡기면 줄이 가져간다
        PasswordHashChallenge        _challenge{}; ///< 로그인의 읽기 단계가 낸 문제
        PasswordHashRecord           _prepared{};  ///< 가입 · 연동의 새 비밀번호, 로그인의 다시 해시(새 솔트 · 설정 매개변수)
        int32                        _restartCount{ 0 };
    };

    namespace
    {
        /** @brief 로그인 요청 · 정리 일이 완료로 넘기는 값입니다. */

        struct LoginCallResult
        {
            LoginCompletion  _completion{};
            LoginStoreResult _serviceResult{};
        };

        /** @brief 비밀번호 로그인의 읽기 단계가 넘기는 값입니다. */
        struct LoginChallengeRead
        {
            PasswordHashChallenge _challenge{};
            LoginGrant            _grant{};
            LoginServiceResult    _result{ LoginResult::Ok };
        };

        /** @brief 비밀번호 로그인의 커밋 단계가 넘기는 값입니다. `_bHashStale` 이면 그사이 솔트 · 매개변수가 바뀌어 읽기부터 다시 한다. */
        struct LoginCommitResult
        {
            LoginCallResult _done{};
            uint8           _bHashStale : 1;

            LoginCommitResult()
                : _bHashStale{ SW_FALSE }
            {
            }
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
                runOperation( connection, crypto, settings, authority, input, done );
                // 워커를 떠나면 비밀번호, 장치 비밀, 외부 주체를 보관하지 않는다. 별도로 지우기(wipe)는 하지 않으며 서버의 관례를 따른다.
                input._credential._password.clear();
                input._subject.clear();
                Memory::wipe( input._arrDeviceSecret, sizeof( input._arrDeviceSecret ) );
                return done;
            }

            static void runOperation( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings, const LoginTicketAuthority& authority,
                                      LoginCallInput& input, LoginCallResult& done )
            {
                LoginCompletion& completion = done._completion;
                LoginStoreLogic  logic{ connection, crypto, settings, authority, done._serviceResult };
                const int64      nowMs = input._nowMs.count();
                switch ( input._operation )
                {
                    case LoginOperation::Register:
                    case LoginOperation::Login:
                    case LoginOperation::LinkCredential:
                    {
                        // 비밀번호 요청은 해시 줄을 지나는 길(`submitPasswordCall`)로만 온다 — 저장소 일에서 해시하지 않는다.
                        SW_ASSERT( false && "password requests go through LoginService::submitPasswordCall" );
                        completion._result = LoginServiceResult::fail( OnlineError::kUnavailable );
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
                    case LoginOperation::IssueLoginNonce:
                    {
                        completion._result = LoginServiceResult::fail( OnlineError::kInternal ); // 저장 일로 맡기지 않는다(`issueLoginNonce`)
                        break;
                    }
                }
                if ( completion._result == LoginResult::Ok && input._bUpdateRecommended == SW_TRUE )
                {
                    completion._grant._bUpdateRecommended = SW_TRUE;
                    completion._grant._storeURL           = input._storeURL;
                }
            }

            static LoginStoreResult runMaintenance( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings, const LoginTicketAuthority& authority,
                                                    const LoginMaintenanceInput& input )
            {
                LoginStoreResult serviceResult;
                LoginStoreLogic  logic{ connection, crypto, settings, authority, serviceResult };
                if ( input._disconnectedSessionID != 0 )
                    logic.markDisconnected( input._disconnectedSessionID, input._nowMs.count() );
                if ( input._listOnline.empty() == false )
                    logic.refreshSessions( input._listOnline, input._nowMs.count() );
                if ( input._purgeCount > 0 )
                    (void)logic.purgeDueDeletions( input._nowMs.count(), input._purgeCount, input._listAfterDeleteAccount );
                return serviceResult;
            }

            /** @brief 비밀번호 요청의 완료 틀입니다(작업 종류 · 요청 태그). */
            static LoginCompletion makePasswordCompletion( const LoginPasswordCall& call )
            {
                LoginCompletion completion;
                completion._operation  = call._input._operation;
                completion._requestTag = call._input._requestTag;
                return completion;
            }

            /** @brief 비밀번호 로그인의 읽기 단계입니다(저장소 스레드). 비밀번호는 오지 않는다. */
            static LoginChallengeRead runReadChallenge( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings,
                                                        const LoginTicketAuthority& authority, const string& loginName, int64 nowMs )
            {
                LoginStoreResult   unused;
                LoginStoreLogic    logic{ connection, crypto, settings, authority, unused };
                LoginChallengeRead read;
                read._result = logic.readLoginChallenge( loginName, nowMs, read._challenge, read._grant );
                return read;
            }

            /** @brief 가입 · 이름 연동의 커밋 단계입니다(저장소 스레드). @p input 의 비밀번호는 이미 지웠다 — 해시를 마친 @p prepared 만 쓴다. */
            static LoginCallResult runNewPasswordCommit( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings,
                                                         const LoginTicketAuthority& authority, const LoginCallInput& input, const PasswordHashRecord& prepared )
            {
                LoginCallResult done;
                done._completion._operation  = input._operation;
                done._completion._requestTag = input._requestTag;
                LoginStoreLogic logic{ connection, crypto, settings, authority, done._serviceResult };
                if ( input._operation == LoginOperation::Register )
                {
                    uint64 accountID                        = 0;
                    done._completion._result                = logic.registerAccount( input._credential, prepared, &accountID );
                    done._completion._identity._accountID   = accountID;
                    done._completion._identity._displayName = input._credential._loginName;
                    return done;
                }
                done._completion._result = logic.linkCredential( input._token, input._credential, prepared, input._nowMs.count(), done._completion._identity );
                return done;
            }

            /** @brief 비밀번호 로그인의 커밋 단계입니다(저장소 스레드). */
            static LoginCommitResult runLoginCommit( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings, const LoginTicketAuthority& authority,
                                                     const LoginCallInput& input, const PasswordHashChallenge& challenge, const PasswordHashResult& hashResult )
            {
                LoginCommitResult commit;
                commit._done._completion._operation  = input._operation;
                commit._done._completion._requestTag = input._requestTag;
                LoginStoreLogic logic{ connection, crypto, settings, authority, commit._done._serviceResult };
                bool            bHashStale       = false;
                commit._done._completion._result = logic.commitLogin( challenge, hashResult, input._nowMs.count(), commit._done._completion._grant, bHashStale );
                commit._bHashStale               = bHashStale ? SW_TRUE : SW_FALSE;
                return commit;
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
        , _listAfterDeleteAccount{}
        , _hashQueue{}
        , _mapPasswordCall{}
        , _mapHashJobToCall{}
        , _listHashCompletionScratch{}
        , _nextPasswordCallID{ 1 }
    {
    }

    LoginService::~LoginService() = default; // 비밀번호 요청(`LoginPasswordCall`)이 이 파일에서 완전한 형식이다

    expected<void> LoginService::initialize( ServiceStore* pStore, ILoginCrypto* pCrypto, IPasswordHasher* pHasher, TaskManager* pTaskManager, const LoginServiceConfig& settings,
                                             const uint8 ( &arrTicketMasterKey )[LoginTicketAuthority::kMasterKeySize] )
    {
        // 비밀번호 해시는 작업 풀 위의 상한 있는 줄에서만 돈다 — 작업 풀 · 해시 구현이 없으면 기동 오류다(저장소 워커로 대신하지 않는다, 감사 O-3).
        if ( pTaskManager == nullptr || pHasher == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the login service needs the task manager and a password hasher - password hashing does not run on store workers" } );
        PasswordHashQueueConfig hashSettings;
        hashSettings._maxConcurrentPasswordHashes = settings._maxConcurrentPasswordHashes;
        hashSettings._maxQueuedPasswordHashes     = settings._maxQueuedPasswordHashes;
        if ( expected<void> queued = _hashQueue.initialize( *pTaskManager, *pHasher, hashSettings ); queued.has_value() == false )
            return queued;
        _store.bindStore( pStore );
        _pCrypto  = pCrypto;
        _settings = settings;
        _revocationScanJob.setPeriod( settings._revocationScanMs );
        _ticketAuthority.initialize( pCrypto, arrTicketMasterKey );
        _mapClientToAttemptBucket.initialize( settings._attemptBurst, settings._attemptRefillMs, kMaxTrackedClientCount );
        return {};
    }

    void LoginService::shutdown()
    {
        // 도는 해시는 끝까지 기다리고(워커가 쓰는 칸을 먼저 놓지 않는다) 답은 버린다 — 연결이 닫히며 호스트가 답 없는 요청을 끝낸다.
        _listHashCompletionScratch.clear();
        _hashQueue.shutdown( _listHashCompletionScratch );
        _listHashCompletionScratch.clear();
        for ( auto& entry : _mapPasswordCall )
        {
            StringUtil::wipeSecret( entry.second->_input._credential._password );
        }
        _mapPasswordCall.clear();
        _mapHashJobToCall.clear();
        _ticketAuthority.shutdown();
        _mapAccountToSession.clear();
        _mapAccountToIdentity.clear();
        _mapNameKeyToAccount.clear();
        _verifications.cancelAll(); // 답을 기다리던 요청은 버린다 — 연결이 닫히며 호스트가 답 없는 요청을 끝낸다
        _cacheReplyOwner.reset();   // 기다리던 캐시 답이 이 객체를 부르지 않게 한다
        _cacheRequests.cancelAll();
        _pRouter = nullptr;
        _revocations.cancelAll(); // 철회 대기열 항목은 저장소에 남아 다음 기동이 다시 한다
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
        _listHashCompletionScratch.clear();
        (void)_hashQueue.pollCompletions( _listHashCompletionScratch );
        for ( const PasswordHashCompletion& completion : _listHashCompletionScratch )
        {
            continueAfterHash( completion );
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

    void LoginService::attachCache( EphemeralStoreRouter* pRouter )
    {
        SW_ASSERT( pRouter != nullptr );
        _pRouter         = pRouter;
        _cacheReplyOwner = pRouter->openReplyOwner();
    }

    void LoginService::onCacheReply( const EphemeralReply& reply )
    {
        (void)_cacheRequests.answer( reply._requestID, reply ); // 모르는 번호는 취소한 일의 늦은 답이다
    }

    template <typename TContinue>
    void LoginService::validateSharedAttempt( uint64 clientKey, LoginOperation operation, uint64 requestTag, const LoginGrant& grant, TContinue&& onAllowed )
    {
        if ( _pRouter == nullptr || _settings._sharedAttemptLimit <= 0 )
        {
            onAllowed();
            return;
        }
        StringBuilder<constant::buffer::kMax64> key;
        key.appendFormat( "%#%#", LoginServiceInternal::kAttemptKeyPrefix, ServiceKeyUtil::makeHex64( clientKey ) );
        const uint64 requestID = _pRouter->submit( EphemeralRequest::makeIncrement( key.view(), 1, _settings._sharedAttemptWindowMs ),
                                                   EphemeralStoreRouter::ReplyDelegate::create<&LoginService::onCacheReply>( this ), _cacheReplyOwner );
        if ( requestID == 0 )
        {
            onAllowed(); // 맡기지 못했다(라우터가 오류 로그를 남겼다) — 이 서버의 버킷은 이미 지났다
            return;
        }
        const bool bTracked = _cacheRequests.track( requestID, 0, [this, operation, requestTag, grant, onAllowed = std::forward<TContinue>( onAllowed )]( const EphemeralReply& reply ) mutable
        {
            const bool bOverLimit = reply._result == EphemeralResult::Ok && reply._integer > _settings._sharedAttemptLimit;
            if ( bOverLimit == false )
            {
                onAllowed(); // 캐시에 닿지 못했으면(Unavailable) 이 서버의 버킷만으로 막는다
                return;
            }
            LoginGrant limited    = grant;
            limited._retryAfterMs = _settings._sharedAttemptWindowMs; // 창이 끝날 때까지 — 위 끝이다
            pushImmediate( operation, requestTag, LoginServiceResult::fail( OnlineError::kRateLimited ), limited );
        } );
        SW_ASSERT( bTracked ); // 라우터 요청 번호는 겹치지 않는다
        (void)bTracked;
    }

    void LoginService::issueLoginNonce( uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginGrant limited;
        if ( consumeAttempt( clientKey, nowMs, limited._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::IssueLoginNonce, requestTag, LoginServiceResult::fail( OnlineError::kRateLimited ), limited );
            return;
        }
        uint8      arrNonce[LoginConstant::kLoginNonceSize]{};
        const bool bReady = _pRouter != nullptr && _settings._loginNonceLifetimeMs > 0 && _pCrypto != nullptr &&
                            _pCrypto->fillRandom( arrNonce, LoginConstant::kLoginNonceSize );
        if ( bReady == false )
        {
            pushImmediate( LoginOperation::IssueLoginNonce, requestTag, LoginServiceResult::fail( OnlineError::kUnavailable ), LoginGrant{} );
            return;
        }
        string nonce;
        for ( int32 offset = 0; offset < LoginConstant::kLoginNonceSize; offset += static_cast<int32>( sizeof( uint64 ) ) )
        {
            uint64 word = 0;
            Memory::copy( &word, arrNonce + offset, sizeof( uint64 ) );
            nonce += ServiceKeyUtil::makeHex64( word );
        }
        Memory::wipe( arrNonce, sizeof( arrNonce ) );
        StringBuilder<constant::buffer::kMax128> nonceKey;
        nonceKey.appendFormat( "%#%#", LoginServiceInternal::kNonceKeyPrefix, nonce );
        const uint64 requestID =
            _pRouter->submit( EphemeralRequest::makeSet( nonceKey.view(), vector<uint8>{ 1 }, _settings._loginNonceLifetimeMs, EphemeralCondition::IfAbsent ),
                              EphemeralStoreRouter::ReplyDelegate::create<&LoginService::onCacheReply>( this ), _cacheReplyOwner );
        if ( requestID == 0 )
        {
            pushImmediate( LoginOperation::IssueLoginNonce, requestTag, LoginServiceResult::fail( OnlineError::kUnavailable ), LoginGrant{} );
            return;
        }
        (void)_cacheRequests.track( requestID, 0, [this, requestTag, nonce = std::move( nonce )]( const EphemeralReply& reply )
        {
            LoginCompletion completion;
            completion._operation  = LoginOperation::IssueLoginNonce;
            completion._requestTag = requestTag;
            completion._result     = reply._result == EphemeralResult::Ok ? LoginServiceResult{ LoginResult::Ok } : LoginServiceResult::fail( OnlineError::kUnavailable );
            if ( reply._result == EphemeralResult::Ok )
                completion._loginNonce = nonce;
            if ( requestTag != 0 )
                _completionBuffer.push( std::move( completion ) );
        } );
    }

    void LoginService::registerAccount( const LoginCredential& credential, uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginGrant limited; // 가입도 비싼 비밀번호 해시를 돌린다 - 로그인과 같은 버킷으로 막지 않으면 해시로 서버를 묶을 수 있다
        if ( consumeAttempt( clientKey, nowMs, limited._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::Register, requestTag, LoginServiceResult::fail( OnlineError::kRateLimited ), limited );
            return;
        }
        validateSharedAttempt( clientKey, LoginOperation::Register, requestTag, limited, [this, credential, nowMs, requestTag]()
        { seedAccount( credential, nowMs, requestTag ); } );
    }

    void LoginService::seedAccount( const LoginCredential& credential, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginCallInput input = makeCallInput( LoginOperation::Register, requestTag, nowMs );
        input._credential    = credential;
        if ( submitPasswordCall( std::move( input ) ) == false && requestTag == 0 )
            SW_LOG_WARNING_ID( "LoginService.SeedAccountBusy", "seed account '%#' was not registered - the password hash queue is full (raise _maxQueuedPasswordHashes)", credential._loginName.c_str() );
    }

    void LoginService::login( const LoginCredential& credential, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginGrant               buildGrant;
        const LoginServiceResult build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::Login, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::Login, requestTag, LoginServiceResult::fail( OnlineError::kRateLimited ), buildGrant );
            return;
        }
        LoginCallInput input      = makeCallInput( LoginOperation::Login, requestTag, nowMs );
        input._credential         = credential;
        input._bUpdateRecommended = buildGrant._bUpdateRecommended;
        input._storeURL           = buildGrant._storeURL;
        validateSharedAttempt( clientKey, LoginOperation::Login, requestTag, buildGrant, [this, input = std::move( input )]() mutable
        { (void)submitPasswordCall( std::move( input ) ); } ); // 해시 자리가 없으면 kBusy 완료를 이미 쌓았다
    }

    void LoginService::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs,
                                   uint64 requestTag )
    {
        LoginGrant               buildGrant;
        const LoginServiceResult build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::GuestLogin, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::GuestLogin, requestTag, LoginServiceResult::fail( OnlineError::kRateLimited ), buildGrant );
            return;
        }
        LoginCallInput input = makeCallInput( LoginOperation::GuestLogin, requestTag, nowMs );
        Memory::copy( input._arrDeviceSecret, arrDeviceSecret, LoginConstant::kDeviceSecretSize );
        input._bUpdateRecommended = buildGrant._bUpdateRecommended;
        input._storeURL           = buildGrant._storeURL;
        validateSharedAttempt( clientKey, LoginOperation::GuestLogin, requestTag, buildGrant, [this, input = std::move( input )]() mutable
        { submitCall( std::move( input ) ); } );
    }

    void LoginService::platformLogin( string_view provider, const vector<uint8>& ticketBytes, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs,
                                      uint64 requestTag )
    {
        LoginGrant               buildGrant;
        const LoginServiceResult build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::PlatformLogin, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::PlatformLogin, requestTag, LoginServiceResult::fail( OnlineError::kRateLimited ), buildGrant );
            return;
        }
        validateSharedAttempt( clientKey, LoginOperation::PlatformLogin, requestTag, buildGrant, [this, provider = string( provider ), ticketBytes, buildGrant, nowMs, requestTag]()
        { beginVerification( LoginOperation::PlatformLogin, provider, ticketBytes, LoginSessionToken{}, buildGrant, nowMs, requestTag ); } );
    }

    void LoginService::resumeSession( const LoginSessionToken& token, const AccountClientInfo& clientInfo, uint64 clientKey, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LoginGrant               buildGrant;
        const LoginServiceResult build = evaluateClientBuild( clientInfo, buildGrant );
        if ( build != LoginResult::Ok )
        {
            pushImmediate( LoginOperation::Resume, requestTag, build, buildGrant );
            return;
        }
        if ( consumeAttempt( clientKey, nowMs, buildGrant._retryAfterMs ) == false )
        {
            pushImmediate( LoginOperation::Resume, requestTag, LoginServiceResult::fail( OnlineError::kRateLimited ), buildGrant );
            return;
        }
        LoginCallInput input      = makeCallInput( LoginOperation::Resume, requestTag, nowMs );
        input._token              = token;
        input._bUpdateRecommended = buildGrant._bUpdateRecommended;
        input._storeURL           = buildGrant._storeURL;
        validateSharedAttempt( clientKey, LoginOperation::Resume, requestTag, buildGrant, [this, input = std::move( input )]() mutable
        { submitCall( std::move( input ) ); } );
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
        (void)submitPasswordCall( std::move( input ) ); // 자리가 없으면 kBusy 완료를 이미 쌓았다
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
        input._nowMs                  = nowMs;
        input._purgeCount             = maxCount;
        input._listAfterDeleteAccount = _listAfterDeleteAccount;
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
        for ( int32 validateIndex = 0; validateIndex < checkCount; ++validateIndex )
        {
            input._listOnline.push_back( listOnline[( _refreshCursor + static_cast<size_t>( validateIndex ) ) % listOnline.size()] );
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
        { applyCompletion( std::move( done._completion ), done._serviceResult ); } );
    }

    bool LoginService::submitPasswordCall( LoginCallInput&& input )
    {
        // 자리부터 잡는다 — 없으면 읽기 전에 kBusy(다시 시도, 멱등 응답으로 기억하지 않는다). 계정이 있든 없든 같은 답이다.
        optional<PasswordHashTicket> ticket = _hashQueue.tryReserve();
        if ( ticket.has_value() == false )
        {
            StringUtil::wipeSecret( input._credential._password );
            pushImmediate( input._operation, input._requestTag, LoginServiceResult::fail( OnlineError::kBusy ), LoginGrant{} );
            return false;
        }
        if ( input._operation != LoginOperation::Login )
        {
            // 가입 · 연동은 형식 밖 입력에 해시를 치르지 않는다(자리는 티켓 소멸이 돌려준다).
            const LoginServiceResult valid = LoginStoreLogic::validateNewCredential( input._credential );
            if ( valid != LoginResult::Ok )
            {
                StringUtil::wipeSecret( input._credential._password );
                pushImmediate( input._operation, input._requestTag, valid, LoginGrant{} );
                return true;
            }
        }
        const uint64                  callID = _nextPasswordCallID++;
        unique_ptr<LoginPasswordCall> pCall  = make_unique<LoginPasswordCall>();
        pCall->_input                        = std::move( input );
        pCall->_ticket                       = std::move( ticket );
        LoginPasswordCall& call              = *pCall;
        _mapPasswordCall.emplace( callID, std::move( pCall ) );
        if ( call._input._operation == LoginOperation::Login )
            submitLoginRead( callID );
        else
            submitPasswordHash( callID, call );
        return true;
    }

    void LoginService::submitLoginRead( uint64 callID )
    {
        const auto callIt = _mapPasswordCall.find( callID );
        if ( callIt == _mapPasswordCall.end() )
            return;
        ILoginCrypto*               pCrypto    = _pCrypto;
        const LoginTicketAuthority* pAuthority = &_ticketAuthority;
        const string                loginName  = callIt->second->_input._credential._loginName; // 비밀번호는 저장소 일로 가지 않는다
        const int64                 nowMs      = callIt->second->_input._nowMs.count();
        _store.submit( [pCrypto, pAuthority, settings = _settings, loginName, nowMs]( IServiceStoreConnection& connection )
        { return LoginStoreRun::runReadChallenge( connection, *pCrypto, settings, *pAuthority, loginName, nowMs ); },
                       [this, callID]( LoginChallengeRead& read )
        {
            const auto readIt = _mapPasswordCall.find( callID );
            if ( readIt == _mapPasswordCall.end() )
                return; // 서비스를 내렸다
            LoginPasswordCall& call = *readIt->second;
            if ( read._result != LoginResult::Ok )
            {
                LoginCompletion completion = LoginStoreRun::makePasswordCompletion( call );
                completion._result         = read._result;
                completion._grant          = read._grant;
                finishPasswordCall( callID, std::move( completion ), LoginStoreResult{} );
                return;
            }
            call._challenge = read._challenge;
            submitPasswordHash( callID, call );
        } );
    }

    void LoginService::submitPasswordHash( uint64 callID, LoginPasswordCall& call )
    {
        SW_ASSERT( call._ticket.has_value() && "a password call holds its hash queue seat until it hashes" );
        PasswordHashRequest request;
        request._password = call._input._credential._password;
        bool bSaltFilled  = true;
        if ( call._input._operation == LoginOperation::Login )
        {
            request._arrSpec[0]._params = call._challenge._params;
            Memory::copy( request._arrSpec[0]._arrSalt, call._challenge._arrSalt, LoginConstant::kSaltSize );
            request._specCount = 1;
            if ( call._challenge._bRehash == SW_TRUE )
            {
                // 매개변수가 바뀐 계정 — 비밀번호를 아는 지금 새 솔트 · 설정 매개변수로도 계산해 둔다(커밋이 바꾼다).
                call._prepared._params      = _settings._passwordHashParams;
                bSaltFilled                 = _pCrypto->fillRandom( call._prepared._arrSalt, LoginConstant::kSaltSize );
                request._arrSpec[1]._params = call._prepared._params;
                Memory::copy( request._arrSpec[1]._arrSalt, call._prepared._arrSalt, LoginConstant::kSaltSize );
                request._specCount = 2;
            }
        }
        else
        {
            call._prepared._params      = _settings._passwordHashParams;
            bSaltFilled                 = _pCrypto->fillRandom( call._prepared._arrSalt, LoginConstant::kSaltSize );
            request._arrSpec[0]._params = call._prepared._params;
            Memory::copy( request._arrSpec[0]._arrSalt, call._prepared._arrSalt, LoginConstant::kSaltSize );
            request._specCount = 1;
            StringUtil::wipeSecret( call._input._credential._password ); // 가입 · 연동은 다시 해시할 일이 없다 — 줄에 넘긴 사본만 남는다
        }
        if ( bSaltFilled == false )
        {
            StringUtil::wipeSecret( request._password );
            LoginCompletion completion = LoginStoreRun::makePasswordCompletion( call );
            completion._result         = LoginServiceResult::fail( OnlineError::kUnavailable );
            finishPasswordCall( callID, std::move( completion ), LoginStoreResult{} );
            return;
        }
        const uint64 jobID = _hashQueue.submit( std::move( *call._ticket ), std::move( request ) );
        call._ticket.reset();
        _mapHashJobToCall[jobID] = callID;
    }

    void LoginService::continueAfterHash( const PasswordHashCompletion& completion )
    {
        const auto jobIt = _mapHashJobToCall.find( completion._jobID );
        if ( jobIt == _mapHashJobToCall.end() )
            return;
        const uint64 callID = jobIt->second;
        _mapHashJobToCall.erase( jobIt );
        const auto callIt = _mapPasswordCall.find( callID );
        if ( callIt == _mapPasswordCall.end() )
            return;
        LoginPasswordCall& call = *callIt->second;
        if ( completion._bComputed == SW_FALSE )
        {
            LoginCompletion failed = LoginStoreRun::makePasswordCompletion( call );
            failed._result         = LoginServiceResult::fail( OnlineError::kUnavailable );
            finishPasswordCall( callID, std::move( failed ), LoginStoreResult{} );
            return;
        }
        ILoginCrypto*               pCrypto    = _pCrypto;
        const LoginTicketAuthority* pAuthority = &_ticketAuthority;
        LoginCallInput              input      = call._input;
        StringUtil::wipeSecret( input._credential._password ); // 커밋은 해시만 쓴다 — 비밀번호는 저장소 일로 가지 않는다
        if ( call._input._operation != LoginOperation::Login )
        {
            Memory::copy( call._prepared._arrHash, completion._arrHash[0], LoginConstant::kPasswordHashSize );
            _store.submit( [pCrypto, pAuthority, settings = _settings, input = std::move( input ), prepared = call._prepared]( IServiceStoreConnection& connection )
            { return LoginStoreRun::runNewPasswordCommit( connection, *pCrypto, settings, *pAuthority, input, prepared ); },
                           [this, callID]( LoginCallResult& done )
            { finishPasswordCall( callID, std::move( done._completion ), done._serviceResult ); } );
            return;
        }
        PasswordHashResult hashResult;
        Memory::copy( hashResult._arrHash, completion._arrHash[0], LoginConstant::kPasswordHashSize );
        if ( call._challenge._bRehash == SW_TRUE )
        {
            hashResult._rehash = call._prepared;
            Memory::copy( hashResult._rehash._arrHash, completion._arrHash[1], LoginConstant::kPasswordHashSize );
            hashResult._bHasRehash = SW_TRUE;
        }
        _store.submit( [pCrypto, pAuthority, settings = _settings, input = std::move( input ), challenge = call._challenge, hashResult]( IServiceStoreConnection& connection )
        { return LoginStoreRun::runLoginCommit( connection, *pCrypto, settings, *pAuthority, input, challenge, hashResult ); },
                       [this, callID]( LoginCommitResult& commit )
        {
            const auto commitIt = _mapPasswordCall.find( callID );
            if ( commitIt == _mapPasswordCall.end() )
                return; // 서비스를 내렸다
            LoginPasswordCall& pending = *commitIt->second;
            if ( commit._bHashStale == SW_TRUE && pending._restartCount < LoginStoreLogic::kMaxHashRestartCount )
            {
                // 해시하는 사이 비밀번호 · 매개변수가 바뀌었다 — 읽기부터 다시 한다. 자리를 다시 잡는다(없으면 kBusy).
                ++pending._restartCount;
                pending._ticket = _hashQueue.tryReserve();
                if ( pending._ticket.has_value() )
                {
                    submitLoginRead( callID );
                    return;
                }
                LoginCompletion busy = LoginStoreRun::makePasswordCompletion( pending );
                busy._result         = LoginServiceResult::fail( OnlineError::kBusy );
                finishPasswordCall( callID, std::move( busy ), LoginStoreResult{} );
                return;
            }
            finishPasswordCall( callID, std::move( commit._done._completion ), commit._done._serviceResult );
        } );
    }

    void LoginService::finishPasswordCall( uint64 callID, LoginCompletion&& completion, const LoginStoreResult& serviceResult )
    {
        const auto callIt = _mapPasswordCall.find( callID );
        if ( callIt == _mapPasswordCall.end() )
            return;
        LoginPasswordCall& call = *callIt->second;
        if ( completion._result == LoginResult::Ok && call._input._bUpdateRecommended == SW_TRUE )
        {
            completion._grant._bUpdateRecommended = SW_TRUE;
            completion._grant._storeURL           = call._input._storeURL;
        }
        StringUtil::wipeSecret( call._input._credential._password );
        _mapPasswordCall.erase( callIt );
        applyCompletion( std::move( completion ), serviceResult );
    }

    void LoginService::submitMaintenance( LoginMaintenanceInput&& input )
    {
        ILoginCrypto*               pCrypto    = _pCrypto;
        const LoginTicketAuthority* pAuthority = &_ticketAuthority;
        _store.submit( [pCrypto, pAuthority, settings = _settings, input = std::move( input )]( IServiceStoreConnection& connection )
        { return LoginStoreRun::runMaintenance( connection, *pCrypto, settings, *pAuthority, input ); },
                       [this]( const LoginStoreResult& serviceResult )
        {
            LoginCompletion none;
            none._requestTag = 0; // 요청 태그 0은 완료 목록에 쌓지 않는다(applyCompletion)
            applyCompletion( std::move( none ), serviceResult );
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

    void LoginService::applyCompletion( LoginCompletion&& completion, const LoginStoreResult& serviceResult )
    {
        for ( const LoginSessionRef& revoked : serviceResult._listRevoked )
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
        for ( const LoginSessionRef& offline : serviceResult._listOffline )
        {
            const auto onlineIt = _mapAccountToSession.find( offline._accountID );
            if ( onlineIt != _mapAccountToSession.end() && onlineIt->second == offline._sessionID )
                _mapAccountToSession.erase( onlineIt );
        }
        for ( const LoginSessionRef& disconnected : serviceResult._listDisconnected )
        {
            const auto onlineIt = _mapAccountToSession.find( disconnected._accountID );
            if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != disconnected._sessionID )
                continue;
            _mapAccountToSession.erase( onlineIt );
            _eventBuffer.push( LoginEvent{ disconnected._accountID, disconnected._sessionID, LoginRevokeReason::None, LoginEvent::Kind::Disconnected } );
        }
        for ( const LoginSessionRef& online : serviceResult._listOnline )
        {
            const auto onlineIt = _mapAccountToSession.find( online._accountID );
            if ( onlineIt != _mapAccountToSession.end() && onlineIt->second != online._sessionID )
                _eventBuffer.push( LoginEvent{ online._accountID, onlineIt->second, LoginRevokeReason::DuplicateLogin, LoginEvent::Kind::Revoked } );
            _mapAccountToSession[online._accountID] = online._sessionID;
        }
        for ( const LoginEvent& event : serviceResult._listEvent )
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

    void LoginService::pushImmediate( LoginOperation operation, uint64 requestTag, LoginServiceResult result, const LoginGrant& grant )
    {
        LoginCompletion completion;
        completion._operation  = operation;
        completion._requestTag = requestTag;
        completion._result     = result;
        completion._grant      = grant;
        _completionBuffer.push( std::move( completion ) );
    }

    LoginServiceResult LoginService::evaluateClientBuild( const AccountClientInfo& clientInfo, LoginGrant& outGrant ) const
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
            return LoginServiceResult::fail( OnlineError::kUpdateRequired );
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
        string nonce;
        if ( pProvider->findServerNonce( ticketBytes, nonce ) == false )
        {
            startVerification( operation, provider, ticketBytes, token, buildGrant, nowMs, requestTag );
            return;
        }
        // 서버가 낸 nonce 를 먼저 지운다 — 지워졌으면(Ok) 처음 쓰는 것이다. 없으면 이미 썼거나 서버가 낸 적 없다. 휘발 저장소 하나라 서버 여럿에서 한 번이다(감사 O-13).
        if ( LoginServiceInternal::isValidLoginNonce( nonce ) == false || _pRouter == nullptr )
        {
            pushImmediate( operation, requestTag, LoginResult::ProviderRejected, LoginGrant{} );
            return;
        }
        StringBuilder<constant::buffer::kMax128> nonceKey;
        nonceKey.appendFormat( "%#%#", LoginServiceInternal::kNonceKeyPrefix, nonce );
        const uint64 requestID = _pRouter->submit( EphemeralRequest::makeErase( nonceKey.view() ),
                                                   EphemeralStoreRouter::ReplyDelegate::create<&LoginService::onCacheReply>( this ), _cacheReplyOwner );
        if ( requestID == 0 )
        {
            pushImmediate( operation, requestTag, LoginResult::ProviderUnavailable, LoginGrant{} );
            return;
        }
        (void)_cacheRequests.track( requestID, 0, [this, operation, provider = string( provider ), ticketBytes, token, buildGrant, nowMs, requestTag]( const EphemeralReply& reply )
        {
            if ( reply._result == EphemeralResult::Ok )
                startVerification( operation, provider, ticketBytes, token, buildGrant, nowMs, requestTag );
            else if ( reply._result == EphemeralResult::NotFound )
                pushImmediate( operation, requestTag, LoginResult::ProviderRejected, LoginGrant{} ); // 다시 쓴 토큰이거나 지난 nonce 다
            else
                pushImmediate( operation, requestTag, LoginResult::ProviderUnavailable, LoginGrant{} ); // 캐시에 닿지 못했다 — 새 nonce 로 다시 한다
        } );
    }

    void LoginService::startVerification( LoginOperation operation, string_view provider, const vector<uint8>& ticketBytes, const LoginSessionToken& token,
                                          const LoginGrant& buildGrant, UnixMilliseconds nowMs, uint64 requestTag )
    {
        IPlatformLoginProvider* pProvider = _registeredPlatformProvider.findByName( provider );
        if ( pProvider == nullptr )
        {
            pushImmediate( operation, requestTag, LoginResult::ProviderUnavailable, LoginGrant{} );
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
            const LoginServiceResult result = verification._bUnavailable == SW_TRUE ? LoginResult::ProviderUnavailable : LoginResult::ProviderRejected;
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
