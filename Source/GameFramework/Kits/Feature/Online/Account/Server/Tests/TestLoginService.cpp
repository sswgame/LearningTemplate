// LoginServiceTest — 로그인 서비스(메모리 저장소, 가짜 암호 · 가짜 제공자): 시도 제한(가입 포함), 빌드 버전, 이벤트와 접속 테이블, 중복 로그인, 끊김과 재접속, 다른 서버가 민 세션,
// 외부 로그인과 연동, 디렉터리(접속한 계정만 · 이름 바뀜), 게임 티켓. 저장 규칙은 LoginStoreLogicTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/FakePlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr int64  kNowMs     = 1700000000000ll;
    constexpr uint64 kClientKey = 77;

    /** @brief 결정적인 가짜 암호입니다. 규칙만 봅니다. */
    class FakeServiceCrypto final : public ILoginCrypto
    {
    public:
        bool fillRandom( uint8* pOut, int32 size ) override
        {
            for ( int32 index = 0; index < size; ++index )
            {
                pOut[index] = static_cast<uint8>( ++_counter );
            }
            return true;
        }
        bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) override
        {
            return fill( pKey, keySize, pInfo, infoSize, pOut, outSize );
        }

    private:
        static bool fill( const uint8* pA, int32 aSize, const uint8* pB, int32 bSize, uint8* pOut, int32 outSize )
        {
            uint64 hash = HashUtil::kFNVOffset64;
            for ( int32 index = 0; index < outSize; ++index )
            {
                for ( int32 a = 0; a < aSize; ++a )
                {
                    hash = HashUtil::fnvStep64( hash, pA[a] );
                }
                for ( int32 b = 0; b < bSize; ++b )
                {
                    hash = HashUtil::fnvStep64( hash, pB[b] );
                }
                hash        = HashUtil::fnvStep64( hash, static_cast<uint8>( index ) );
                pOut[index] = static_cast<uint8>( hash >> 24 );
            }
            return true;
        }

        uint32 _counter{ 0 };
    };

    /**
     * @brief 결정적인 가짜 비밀번호 해시입니다. 부른 수와 "작업 풀 워커가 아닌 스레드에서 불렸다" 를 셉니다(스레드 안전 — 워커가 부른다).
     * @details 메모리 저장소는 맡긴 스레드(테스트 스레드)에서 일을 돌리므로, 해시가 저장소 일 안에서 돌면 워커가 아닌 스레드에서 불린다(감사 O-3 이전 모양).
     */
    class FakeWorkerHasher final : public IPasswordHasher
    {
    public:
        atomic<int32> _callCount{ 0 };
        atomic<int32> _offWorkerCount{ 0 };

        bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut, int32 outSize ) override
        {
            (void)params;
            const TaskManager* pTaskManager = game::findSubsystem<TaskManager>();
            if ( pTaskManager == nullptr || pTaskManager->isWorkerThread() == false )
                _offWorkerCount.fetch_add( 1, std::memory_order_relaxed );
            _callCount.fetch_add( 1, std::memory_order_relaxed );
            uint64 hash = HashUtil::kFNVOffset64;
            for ( const utf8 character : password )
            {
                hash = HashUtil::fnvStep64( hash, static_cast<uint8>( character ) );
            }
            for ( int32 index = 0; index < saltSize; ++index )
            {
                hash = HashUtil::fnvStep64( hash, pSalt[index] );
            }
            for ( int32 index = 0; index < outSize; ++index )
            {
                hash        = HashUtil::fnvStep64( hash, static_cast<uint8>( index ) );
                pOut[index] = static_cast<uint8>( hash >> 24 );
            }
            return true;
        }
    };

    /** @brief 테스트의 로그인 설정입니다. 해시 줄 상한 · nonce 수명은 키트 설정의 필수 칸이라 기본값이 없다. */
    LoginServiceConfig makeLoginServiceSettings()
    {
        LoginServiceConfig settings;
        settings._maxConcurrentPasswordHashes = 2;
        settings._maxQueuedPasswordHashes     = 64;
        settings._loginNonceLifetimeMs        = 60000; // 키트 설정의 필수 칸(`_loginNonceLifetimeSeconds`) — 0 이면 nonce 를 내지 못한다
        return settings;
    }

    constexpr int64 kHashWaitMs = 10000; ///< 테스트가 해시 하나를 기다리는 상한(실패해도 멈추지 않게)

    /** @brief 서버 하나입니다. 영속 데이터는 빌립니다 — 데이터를 같이 쓰면 "서버 둘" 입니다. 휘발 저장소도 @p pSharedCache 로 나눌 수 있습니다. */
    struct LoginFixture
    {
        ManualClock               _clock;
        MemoryEphemeralDatabase   _ownCacheData;
        MemoryEphemeralStore      _cache;
        EphemeralStoreRouter      _router;
        MemoryServiceStore        _store;
        FakeServiceCrypto         _crypto;
        FakeWorkerHasher          _hasher;
        FakePlatformLoginProvider _provider;
        LoginService              _service;
        uint64                    _nextTag{ 1 };

        explicit LoginFixture( MemoryServiceDatabase& database, const LoginServiceConfig& settings = makeLoginServiceSettings(), MemoryEphemeralDatabase* pSharedCache = nullptr )
            : _clock{ MonotonicMilliseconds{ 1000 } }
            , _ownCacheData{ _clock }
            , _cache{ pSharedCache != nullptr ? pSharedCache : &_ownCacheData }
            , _router{}
            , _store{ &database }
            , _crypto{}
            , _hasher{}
            , _provider{ "fake" }
            , _service{}
        {
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize]{ 1, 2, 3 };
            _router.initialize( &_cache );
            SW_EXPECT_TRUE( _service.initialize( &_store, &_crypto, &_hasher, game::findSubsystem<TaskManager>(), settings, arrMasterKey ).has_value() );
            _service.attachCache( &_router );
            SW_EXPECT_TRUE( _service.registerPlatformProvider( &_provider ) );
        }

        ~LoginFixture()
        {
            poll();
            _store.shutdown();
            (void)_store.pollCompletions();
            _service.shutdown();
            _router.shutdown();
        }

        void poll()
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                (void)_router.poll();                                // 시도 창 답이 해시 줄에 맡긴다
                (void)_service.waitForPasswordHashes( kHashWaitMs ); // 해시는 작업 풀에서 돈다 — 끝난 것을 다음 update 가 잇는다
                _service.update( UnixMilliseconds{ kNowMs } );
                while ( _store.pollCompletions() > 0 )
                {
                }
            }
        }

        LoginCompletion take( uint64 tag )
        {
            poll();
            vector<LoginCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            if ( listCompletion.empty() )
                return LoginCompletion{};
            SW_EXPECT_EQUAL( tag, listCompletion[0]._requestTag );
            return std::move( listCompletion[0] );
        }

        LoginServiceResult registerAccount( string_view name, uint64 clientKey = kClientKey )
        {
            const uint64 tag = _nextTag++;
            _service.registerAccount( LoginCredential{ string( name ), "password123" }, clientKey, UnixMilliseconds{ kNowMs }, tag );
            return take( tag )._result;
        }

        LoginCompletion login( string_view name, const AccountClientInfo& clientInfo = AccountClientInfo{}, uint64 clientKey = kClientKey, string_view password = "password123" )
        {
            const uint64 tag = _nextTag++;
            _service.login( LoginCredential{ string( name ), string( password ) }, clientInfo, clientKey, UnixMilliseconds{ kNowMs }, tag );
            return take( tag );
        }

        LoginCompletion platformLogin( string_view ticket )
        {
            const uint64 tag = _nextTag++;
            _service.platformLogin( "fake", vector<uint8>( ticket.begin(), ticket.end() ), AccountClientInfo{}, kClientKey, UnixMilliseconds{ kNowMs }, tag );
            return take( tag );
        }

        vector<LoginEvent> takeEvents()
        {
            poll();
            vector<LoginEvent> listEvent;
            _service.pollEvents( listEvent );
            return listEvent;
        }

        /** @brief 가입하고 로그인한 뒤 이벤트를 비웁니다. */
        LoginGrant signIn( string_view name )
        {
            SW_EXPECT_TRUE( registerAccount( name ) == LoginResult::Ok );
            const LoginCompletion loggedIn = login( name );
            SW_EXPECT_TRUE( loggedIn._result == LoginResult::Ok );
            (void)takeEvents();
            return loggedIn._grant;
        }
    };
} // namespace

/** @brief [LoginServiceTest] 시도 제한: 가입과 로그인이 한 버킷을 쓰고, 넘치면 기다릴 시간과 함께 RateLimited 이며 저장소에 맡기지 않는다. 주소가 다르면 따로다 */
SW_TEST_CASE( LoginServiceTest, AttemptsAreLimitedPerClientIncludingRegister )
{
    MemoryServiceDatabase database;
    LoginServiceConfig    settings = makeLoginServiceSettings();
    settings._attemptBurst         = 2;
    LoginFixture rig( database, settings );

    SW_EXPECT_TRUE( rig.registerAccount( "alice" ) == LoginResult::Ok );
    SW_EXPECT_TRUE( rig.login( "alice" )._result == LoginResult::Ok );
    const uint64 commitsBefore = database.getCommitCount();

    const uint64 tag = 90;
    rig._service.registerAccount( LoginCredential{ "bob", "password123" }, kClientKey, UnixMilliseconds{ kNowMs }, tag );
    const LoginCompletion limited = rig.take( tag );
    SW_EXPECT_TRUE( limited._result.hasError( OnlineError::kRateLimited ) );
    SW_EXPECT_TRUE( limited._grant._retryAfterMs > 0 );
    SW_EXPECT_EQUAL( commitsBefore, database.getCommitCount() ); // 비싼 해시도 저장도 돌지 않았다

    SW_EXPECT_TRUE( rig.login( "alice" )._result.hasError( OnlineError::kRateLimited ) );
    SW_EXPECT_TRUE( rig.registerAccount( "bob", kClientKey + 1 ) == LoginResult::Ok );
}

/** @brief [LoginServiceTest] 서버가 스스로 만드는 시드 계정은 시도 제한을 쓰지 않는다 — 많이 넣어도 빠지지 않는다 */
SW_TEST_CASE( LoginServiceTest, SeedAccountsAreNotLimited )
{
    MemoryServiceDatabase database;
    LoginServiceConfig    settings = makeLoginServiceSettings();
    settings._attemptBurst         = 1;
    LoginFixture rig( database, settings );
    for ( int32 index = 0; index < 5; ++index )
    {
        const uint64 tag = static_cast<uint64>( index ) + 1;
        rig._service.seedAccount( LoginCredential{ "seed" + to_string( index ), "password123" }, UnixMilliseconds{ kNowMs }, tag );
        SW_EXPECT_TRUE( rig.take( tag )._result == LoginResult::Ok );
    }
}

/** @brief [LoginServiceTest] 빌드 버전: 최소 미만은 UpdateRequired 와 상점 주소이고 저장소에 맡기지 않는다. 권장 미만은 들어오되 권장 표시가 붙는다. 설정이 없으면 보지 않는다 */
SW_TEST_CASE( LoginServiceTest, ClientBuildIsChecked )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    SW_EXPECT_TRUE( rig.registerAccount( "alice" ) == LoginResult::Ok );

    RemoteConfig config;
    const auto   setText = [&]( string_view key, string_view text, int64 version )
    {
        RemoteConfigValue value;
        value._type = RemoteConfigValueType::Text;
        value._text = string( text );
        ServiceAuditEntry audit;
        audit._actor   = "gm.0000000000000001";
        audit._action  = "config.set";
        audit._subject = "config";
        audit._timeMs  = version;
        config.submitSet( rig._store, key, value, audit );
        rig.poll();
    };
    setText( "account.minimum_build.windows", "1.2.0", 1 );
    setText( "account.recommended_build.windows", "1.4.0", 2 );
    setText( "account.store_url.windows", "https://store.example/x", 3 );
    rig._service.setRemoteConfig( &config );

    AccountClientInfo clientInfo;
    clientInfo._platform         = "windows";
    clientInfo._build            = "1.1.9";
    const LoginCompletion tooOld = rig.login( "alice", clientInfo );
    SW_EXPECT_TRUE( tooOld._result.hasError( OnlineError::kUpdateRequired ) );
    SW_EXPECT_EQUAL( string( "https://store.example/x" ), tooOld._grant._storeURL );

    clientInfo._build                 = "1.3.0";
    const LoginCompletion recommended = rig.login( "alice", clientInfo );
    SW_EXPECT_TRUE( recommended._result == LoginResult::Ok );
    SW_EXPECT_TRUE( recommended._grant._bUpdateRecommended == SW_TRUE );
    SW_EXPECT_EQUAL( string( "https://store.example/x" ), recommended._grant._storeURL );

    clientInfo._build             = "1.4.0";
    const LoginCompletion current = rig.login( "alice", clientInfo );
    SW_EXPECT_TRUE( current._result == LoginResult::Ok );
    SW_EXPECT_TRUE( current._grant._bUpdateRecommended == SW_FALSE );
    SW_EXPECT_TRUE( current._grant._storeURL.empty() );
}

/** @brief [LoginServiceTest] 접속 테이블과 디렉터리: 로그인하면 이벤트가 나고 이름 · 계정으로 찾을 수 있다(이름은 대소문자 무시). 로그아웃하면 찾을 수 없다 */
SW_TEST_CASE( LoginServiceTest, DirectoryKnowsOnlineAccountsOnly )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    SW_EXPECT_TRUE( rig.registerAccount( "Alice" ) == LoginResult::Ok );
    AccountIdentity identity;
    SW_EXPECT_FALSE( rig._service.findIdentityByDisplayName( "alice", identity ) ); // 가입만으로는 접속이 아니다

    const LoginGrant   grant     = rig.login( "Alice" )._grant;
    vector<LoginEvent> listEvent = rig.takeEvents();
    SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == LoginEvent::Kind::LoggedIn );

    const AccountID accountID = grant._identity._accountID;
    SW_EXPECT_TRUE( rig._service.isAccountOnline( accountID ) );
    SW_EXPECT_EQUAL( 1, rig._service.getOnlineCount() );
    SW_EXPECT_TRUE( rig._service.findOnlineSessionID( accountID ) != 0 );
    SW_REQUIRE_TRUE( rig._service.findIdentityByDisplayName( "ALICE", identity ) );
    SW_EXPECT_EQUAL( accountID, identity._accountID );
    SW_REQUIRE_TRUE( rig._service.findIdentity( accountID, identity ) );

    rig._service.logout( grant._token, UnixMilliseconds{ kNowMs }, 50 );
    SW_EXPECT_TRUE( rig.take( 50 )._result == LoginResult::Ok );
    SW_EXPECT_FALSE( rig._service.isAccountOnline( accountID ) );
    SW_EXPECT_FALSE( rig._service.findIdentity( accountID, identity ) );
    SW_EXPECT_FALSE( rig._service.findIdentityByDisplayName( "alice", identity ) );
    SW_EXPECT_EQUAL( 0, rig._service.getOnlineCount() );
}

/** @brief [LoginServiceTest] 중복 로그인: 새 로그인이 앞 세션을 민다. 앞 토큰은 Revoked 이고, 밀린 세션을 알리는 이벤트가 난다 */
SW_TEST_CASE( LoginServiceTest, SecondLoginRevokesFirst )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    const LoginGrant      first        = rig.signIn( "alice" );
    const uint64          firstSession = rig._service.findOnlineSessionID( first._identity._accountID );

    const LoginGrant         second    = rig.login( "alice" )._grant;
    const vector<LoginEvent> listEvent = rig.takeEvents();
    bool                     bRevoked  = false;
    for ( const LoginEvent& event : listEvent )
    {
        if ( event._kind == LoginEvent::Kind::Revoked && event._sessionID == firstSession )
        {
            bRevoked = true;
            SW_EXPECT_TRUE( event._reason == LoginRevokeReason::DuplicateLogin );
        }
    }
    SW_EXPECT_TRUE( bRevoked );
    SW_EXPECT_TRUE( rig._service.findOnlineSessionID( first._identity._accountID ) != firstSession );
    SW_EXPECT_EQUAL( 1, rig._service.getOnlineCount() );

    rig._service.validateSession( first._token, UnixMilliseconds{ kNowMs }, 50 );
    SW_EXPECT_TRUE( rig.take( 50 )._result != LoginResult::Ok );
    rig._service.validateSession( second._token, UnixMilliseconds{ kNowMs }, 51 );
    SW_EXPECT_TRUE( rig.take( 51 )._result == LoginResult::Ok );
}

/** @brief [LoginServiceTest] 끊김: 재접속 유예에 들어가 접속 테이블에서 빠지고, 유예 안에 토큰으로 돌아오면 같은 세션이다. 이전 토큰은 무효가 된다 */
SW_TEST_CASE( LoginServiceTest, DisconnectThenResume )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    const LoginGrant      grant     = rig.signIn( "alice" );
    const AccountID       accountID = grant._identity._accountID;
    const uint64          sessionID = rig._service.findOnlineSessionID( accountID );

    rig._service.markDisconnected( sessionID, UnixMilliseconds{ kNowMs } );
    vector<LoginEvent> listEvent = rig.takeEvents();
    SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == LoginEvent::Kind::Disconnected );
    SW_EXPECT_FALSE( rig._service.isAccountOnline( accountID ) );

    rig._service.resumeSession( grant._token, AccountClientInfo{}, kClientKey, UnixMilliseconds{ kNowMs + 1 }, 50 );
    const LoginCompletion resumed = rig.take( 50 );
    SW_EXPECT_TRUE( resumed._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( sessionID, rig._service.findOnlineSessionID( accountID ) );
    listEvent = rig.takeEvents();
    SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == LoginEvent::Kind::Resumed );

    rig._service.resumeSession( grant._token, AccountClientInfo{}, kClientKey, UnixMilliseconds{ kNowMs + 2 }, 51 );
    SW_EXPECT_TRUE( rig.take( 51 )._result != LoginResult::Ok );
}

/** @brief [LoginServiceTest] 서버 둘: 다른 서버의 로그인이 민 세션은 버스 알림(또는 주기 읽기)으로 알아채 접속 테이블에서 뺀다. 지난 세션의 늦은 알림은 새 세션을 건드리지 않는다 */
SW_TEST_CASE( LoginServiceTest, RevocationOnAnotherServerIsNotified )
{
    MemoryServiceDatabase database;
    LoginFixture          rigA( database );
    LoginFixture          rigB( database );
    const LoginGrant      grantA    = rigA.signIn( "alice" );
    const AccountID       accountID = grantA._identity._accountID;
    const uint64          sessionA  = rigA._service.findOnlineSessionID( accountID );

    SW_EXPECT_TRUE( rigB.login( "alice" )._result == LoginResult::Ok );
    vector<LoginEvent> listRemote;
    rigB._service.pollRemoteRevocations( listRemote );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRemote.size() ); // B 는 자기 것이 아닌 세션을 밀었으니 버스로 알린다
    SW_EXPECT_EQUAL( sessionA, listRemote[0]._sessionID );

    rigA._service.recordRevokedElsewhere( accountID, sessionA + 1, LoginRevokeReason::DuplicateLogin ); // 다른 세션의 알림
    SW_EXPECT_TRUE( rigA._service.isAccountOnline( accountID ) );
    rigA._service.recordRevokedElsewhere( accountID, sessionA, LoginRevokeReason::DuplicateLogin );
    SW_EXPECT_FALSE( rigA._service.isAccountOnline( accountID ) );
    AccountIdentity identity;
    SW_EXPECT_FALSE( rigA._service.findIdentity( accountID, identity ) );
    const vector<LoginEvent> listEvent = rigA.takeEvents();
    SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == LoginEvent::Kind::Revoked );
}

/** @brief [LoginServiceTest] 버스 알림을 놓쳐도 주기 읽기가 밀린 세션을 찾아 뺀다 */
SW_TEST_CASE( LoginServiceTest, RefreshFindsMissedRevocation )
{
    MemoryServiceDatabase database;
    LoginFixture          rigA( database );
    LoginFixture          rigB( database );
    const AccountID       accountID = rigA.signIn( "alice" )._identity._accountID;
    SW_EXPECT_TRUE( rigB.login( "alice" )._result == LoginResult::Ok );
    SW_EXPECT_TRUE( rigA._service.isAccountOnline( accountID ) );

    rigA._service.refreshOnlineSessions( UnixMilliseconds{ kNowMs }, 10 );
    rigA.poll();
    SW_EXPECT_FALSE( rigA._service.isAccountOnline( accountID ) );
    SW_EXPECT_TRUE( rigB._service.isAccountOnline( accountID ) );
}

/** @brief [LoginServiceTest] 운영 조치: 세션을 끊으면 Administrative 로 알리고 토큰이 무효가 된다 */
SW_TEST_CASE( LoginServiceTest, RevokeByOperator )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    const LoginGrant      grant     = rig.signIn( "alice" );
    const AccountID       accountID = grant._identity._accountID;
    rig._service.revokeAccountSessions( accountID, LoginRevokeReason::Administrative, UnixMilliseconds{ kNowMs }, 50 );
    SW_EXPECT_TRUE( rig.take( 50 )._result == LoginResult::Ok );
    const vector<LoginEvent> listEvent = rig.takeEvents();
    SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._reason == LoginRevokeReason::Administrative );
    SW_EXPECT_FALSE( rig._service.isAccountOnline( accountID ) );
    rig._service.validateSession( grant._token, UnixMilliseconds{ kNowMs }, 51 );
    SW_EXPECT_TRUE( rig.take( 51 )._result != LoginResult::Ok );
}

/** @brief [LoginServiceTest] 외부 로그인: 제공자 확인 뒤 들어오고 같은 주체는 같은 계정이다. 거절 · 닿지 못함 · 없는 제공자 · 빈 티켓을 나눠 답한다 */
SW_TEST_CASE( LoginServiceTest, PlatformLoginGoesThroughProvider )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    const LoginCompletion first = rig.platformLogin( "subject:sub1:Hero" );
    SW_EXPECT_TRUE( first._result == LoginResult::Ok );
    SW_EXPECT_TRUE( first._grant._identity._accountID != kInvalidAccountID );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );
    SW_EXPECT_EQUAL( first._grant._identity._accountID, rig.platformLogin( "subject:sub1:Hero" )._grant._identity._accountID );
    SW_EXPECT_TRUE( rig.platformLogin( "subject:sub2:Other" )._grant._identity._accountID != first._grant._identity._accountID );

    SW_EXPECT_TRUE( rig.platformLogin( "garbage" )._result == LoginResult::ProviderRejected );
    SW_EXPECT_TRUE( rig.platformLogin( "down" )._result == LoginResult::ProviderUnavailable );
    SW_EXPECT_TRUE( rig.platformLogin( "" )._result == LoginResult::ProviderRejected );
    rig._service.platformLogin( "nope", vector<uint8>{ 1 }, AccountClientInfo{}, kClientKey, UnixMilliseconds{ kNowMs }, 90 );
    SW_EXPECT_TRUE( rig.take( 90 )._result == LoginResult::ProviderUnavailable );
}

/** @brief [LoginServiceTest] 연동으로 게스트가 이름을 얻으면 디렉터리가 새 이름으로 찾고 옛 이름으로는 못 찾는다 */
SW_TEST_CASE( LoginServiceTest, LinkingUpdatesTheDirectory )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    const uint8           arrDeviceSecret[LoginConstant::kDeviceSecretSize]{ 9, 8, 7 };
    rig._service.guestLogin( arrDeviceSecret, AccountClientInfo{}, kClientKey, UnixMilliseconds{ kNowMs }, 50 );
    const LoginCompletion guest = rig.take( 50 );
    SW_REQUIRE_TRUE( guest._result == LoginResult::Ok );
    const string guestName = guest._grant._identity._displayName;
    SW_EXPECT_TRUE( guest._grant._identity._bGuest == SW_TRUE );

    rig._service.linkCredential( guest._grant._token, LoginCredential{ "newname", "password123" }, UnixMilliseconds{ kNowMs }, 51 );
    const LoginCompletion linked = rig.take( 51 );
    SW_EXPECT_TRUE( linked._result == LoginResult::Ok );
    AccountIdentity identity;
    SW_REQUIRE_TRUE( rig._service.findIdentityByDisplayName( "newname", identity ) );
    SW_EXPECT_TRUE( identity._bGuest == SW_FALSE );
    SW_EXPECT_FALSE( rig._service.findIdentityByDisplayName( guestName, identity ) );
}

/** @brief [LoginServiceTest] 게임 티켓: 살아 있는 세션만 받는다 */
SW_TEST_CASE( LoginServiceTest, GameTicketNeedsLiveSession )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    const LoginGrant      grant = rig.signIn( "alice" );
    rig._service.issueGameTicket( grant._token, hashed_string( "game1" ), UnixMilliseconds{ kNowMs }, 50 );
    SW_EXPECT_TRUE( rig.take( 50 )._result == LoginResult::Ok );
    rig._service.logout( grant._token, UnixMilliseconds{ kNowMs }, 51 );
    (void)rig.take( 51 );
    rig._service.issueGameTicket( grant._token, hashed_string( "game1" ), UnixMilliseconds{ kNowMs }, 52 );
    SW_EXPECT_TRUE( rig.take( 52 )._result != LoginResult::Ok );
}

/** @brief [LoginServiceTest] 저장소 실패: 로그인은 StoreUnavailable 이고 접속 테이블이 그대로다. 제공자 이름 규칙 · 겹침은 등록에서 거절한다 */
SW_TEST_CASE( LoginServiceTest, StoreFailureAndProviderRegistration )
{
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    SW_EXPECT_LOG( "LoginService.PlatformLoginProviderNo" );
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    SW_EXPECT_TRUE( rig.registerAccount( "alice" ) == LoginResult::Ok );
    database.armFault( ServiceStoreFault::RejectRead );
    SW_EXPECT_TRUE( rig.login( "alice" )._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( 0, rig._service.getOnlineCount() );

    FakePlatformLoginProvider sameName( "fake" );
    SW_EXPECT_FALSE( rig._service.registerPlatformProvider( &sameName ) );
    FakePlatformLoginProvider badName( "Bad Name" );
    SW_EXPECT_FALSE( rig._service.registerPlatformProvider( &badName ) );
}

/** @brief [LoginServiceTest] 비밀번호 해시는 저장소 일이 아니라 작업 풀 워커에서만 돈다 — 가입 · 로그인 · 틀린 비밀번호 · 없는 이름 모두(감사 O-3) */
SW_TEST_CASE( LoginServiceTest, LoginHashNeverRunsOnAStoreWorker )
{
    SW_REQUIRE_NOT_NULL( game::findSubsystem<TaskManager>() );
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    SW_EXPECT_TRUE( rig.registerAccount( "alice" ) == LoginResult::Ok );
    SW_EXPECT_TRUE( rig.login( "alice" )._result == LoginResult::Ok );
    SW_EXPECT_TRUE( rig.login( "alice", AccountClientInfo{}, kClientKey + 1, "wrong password" )._result == LoginResult::WrongCredentials );
    SW_EXPECT_TRUE( rig.login( "nobody_here", AccountClientInfo{}, kClientKey + 2 )._result == LoginResult::WrongCredentials );
    SW_EXPECT_EQUAL( 4, rig._hasher._callCount.load() );
    SW_EXPECT_EQUAL( 0, rig._hasher._offWorkerCount.load() );
}

/** @brief [LoginServiceTest] 해시 줄 자리(돌기 1 + 기다리기 1)가 차면 다음 로그인은 저장소를 읽기 전에 kBusy 로 끝난다. 받은 요청은 끝까지 간다 */
SW_TEST_CASE( LoginServiceTest, LoginIsBusyWhenTheHashQueueIsFull )
{
    MemoryServiceDatabase database;
    LoginServiceConfig    settings        = makeLoginServiceSettings();
    settings._maxConcurrentPasswordHashes = 1;
    settings._maxQueuedPasswordHashes     = 1;
    LoginFixture rig( database, settings );
    SW_REQUIRE_TRUE( rig.registerAccount( "alice" ) == LoginResult::Ok );

    // 셋을 한꺼번에 — 앞 둘이 자리를 잡고, 셋째는 바로 kBusy 다(거두기 전).
    for ( uint64 tag = 101; tag <= 103; ++tag )
    {
        rig._service.login( LoginCredential{ "alice", "password123" }, AccountClientInfo{}, kClientKey + tag, UnixMilliseconds{ kNowMs }, tag );
    }
    vector<LoginCompletion> listImmediate;
    rig._service.takeCompletions( listImmediate );
    SW_REQUIRE_EQUAL( size_t( 1 ), listImmediate.size() );
    SW_EXPECT_EQUAL( uint64( 103 ), listImmediate[0]._requestTag );
    SW_EXPECT_TRUE( listImmediate[0]._result.hasError( OnlineError::kBusy ) );

    rig.poll();
    vector<LoginCompletion> listDone;
    rig._service.takeCompletions( listDone );
    SW_REQUIRE_EQUAL( size_t( 2 ), listDone.size() );
    for ( const LoginCompletion& done : listDone )
    {
        SW_EXPECT_TRUE( done._result == LoginResult::Ok );
    }
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );
}

/** @brief [LoginServiceTest] 주소 단위 시도 창은 휘발 저장소에서 서버 사이에 나눈다 — 서버마다의 버킷이 넉넉해도 두 서버 합이 상한을 넘으면 kRateLimited 다(감사 O-14). 다른 주소는 따로다 */
SW_TEST_CASE( LoginServiceTest, SharedAttemptWindowSpansServers )
{
    MemoryServiceDatabase database;
    LoginServiceConfig    settings  = makeLoginServiceSettings();
    settings._attemptBurst          = 100; // 서버마다의 버킷은 걸리지 않는다
    settings._sharedAttemptLimit    = 2;
    settings._sharedAttemptWindowMs = 60000;
    LoginFixture serverA( database, settings );
    LoginFixture serverB( database, settings, &serverA._ownCacheData );

    SW_EXPECT_TRUE( serverA.registerAccount( "alice" ) == LoginResult::Ok ); // 가입도 센다
    SW_EXPECT_TRUE( serverB.login( "alice" )._result == LoginResult::Ok );   // 다른 서버의 둘째 시도
    const LoginCompletion limited = serverA.login( "alice" );
    SW_EXPECT_TRUE( limited._result.hasError( OnlineError::kRateLimited ) );
    SW_EXPECT_TRUE( limited._grant._retryAfterMs > 0 );
    SW_EXPECT_TRUE( serverB.registerAccount( "bob", kClientKey + 1 ) == LoginResult::Ok );
}

/** @brief [LoginServiceTest] 외부 로그인 nonce 는 서버가 내고 한 번만 쓴다 — 같은 티켓을 다시 쓰거나, 서버가 내지 않은 nonce · nonce 없는 티켓은 제공자에 닿기 전에 거절한다(감사 O-13) */
SW_TEST_CASE( LoginServiceTest, PlatformNonceIsServerIssuedAndUsedOnce )
{
    MemoryServiceDatabase database;
    LoginFixture          rig( database );
    rig._provider.requireServerNonce();
    rig._service.issueLoginNonce( kClientKey, UnixMilliseconds{ kNowMs }, 50 );
    const LoginCompletion issued = rig.take( 50 );
    SW_REQUIRE_TRUE( issued._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( size_t( LoginConstant::kLoginNonceTextSize ), issued._loginNonce.size() );

    const string ticket = "subject:sub1:Hero|" + issued._loginNonce;
    SW_EXPECT_TRUE( rig.platformLogin( ticket )._result == LoginResult::Ok );
    SW_EXPECT_TRUE( rig.platformLogin( ticket )._result == LoginResult::ProviderRejected ); // 다시 쓴 토큰
    SW_EXPECT_TRUE( rig.platformLogin( "subject:sub1:Hero|" + string( static_cast<size_t>( LoginConstant::kLoginNonceTextSize ), 'a' ) )._result == LoginResult::ProviderRejected );
    SW_EXPECT_TRUE( rig.platformLogin( "subject:sub1:Hero" )._result == LoginResult::ProviderRejected );
    SW_EXPECT_EQUAL( 1, rig._provider.getSubmittedCount() ); // 거절은 제공자에 닿지 않았다
}
