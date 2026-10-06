#include "pch.h"

#include "Core/Common/HashUtil.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Online/Server/Account/LoginService.h"
#include "GameFramework/Kits/Online/Server/Account/NetSecurityLoginCrypto.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>

// 로그인 서비스 — 가입 · 로그인 · 토큰 검증, 틀린 비밀번호 잠금 · 시도 제한, 중복 로그인(밀어내기 · 거절), 재접속 유예 · 토큰 회전 · 만료,
// 서비스 재시작 뒤 재접속, 서버 둘 동시 로그인, 게임 접속 표(서명 · 서버 · 시한 · 변조), 저장소 실패 주입(반쪽 세션 없음), 해시 매개변수 갱신,
// 실제 암호(OpenSSL Argon2id · HKDF — NetSecurityLoginCrypto).

using namespace sw;

namespace
{
    /** @brief 시험용 결정적 "암호" — 암호가 아니다. 난수는 씨앗에서, 해시는 FNV 섞기입니다. 비밀번호 해시를 몇 번 불렀는지 셉니다. */
    class TestLoginCrypto final : public ILoginCrypto
    {
    public:
        int32 _passwordHashCount;

        explicit TestLoginCrypto( uint64 seed )
            : _passwordHashCount{ 0 }
            , _state{ seed == 0 ? 1 : seed }
        {
        }

        bool fillRandom( uint8* pOut, int32 size ) override
        {
            for ( int32 byteIndex = 0; byteIndex < size; ++byteIndex )
            {
                _state ^= _state << 13;
                _state ^= _state >> 7;
                _state ^= _state << 17;
                pOut[byteIndex] = static_cast<uint8>( _state >> 24 );
            }
            return true;
        }

        bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut, int32 outSize ) override
        {
            ++_passwordHashCount;
            const uint64 seed = params._memoryKiB * 31ull + params._iterationCount * 7ull + params._parallelism;
            mix( seed, reinterpret_cast<const uint8*>( password.data() ), static_cast<int32>( password.size() ), pSalt, saltSize, pOut, outSize );
            return true;
        }

        bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) override
        {
            mix( 0x51ull, pKey, keySize, pInfo, infoSize, pOut, outSize );
            return true;
        }

    private:
        static void mix( uint64 seed, const uint8* pFirst, int32 firstSize, const uint8* pSecond, int32 secondSize, uint8* pOut, int32 outSize )
        {
            for ( int32 blockIndex = 0; blockIndex * 8 < outSize; ++blockIndex )
            {
                uint64 hash = sw::HashUtil::kFnvOffset64 ^ ( seed + static_cast<uint64>( blockIndex ) * sw::HashUtil::kGoldenRatio64 );
                for ( int32 index = 0; index < firstSize; ++index )
                    hash = ( hash ^ pFirst[index] ) * sw::HashUtil::kFnvPrime64;
                hash = ( hash ^ 0xFFu ) * sw::HashUtil::kFnvPrime64;
                for ( int32 index = 0; index < secondSize; ++index )
                    hash = ( hash ^ pSecond[index] ) * sw::HashUtil::kFnvPrime64;
                for ( int32 byteIndex = 0; byteIndex < 8 && blockIndex * 8 + byteIndex < outSize; ++byteIndex )
                    pOut[blockIndex * 8 + byteIndex] = static_cast<uint8>( hash >> ( byteIndex * 8 ) );
            }
        }

        uint64 _state;
    };

    struct TestLoginServiceInternal
    {
        static constexpr uint8 kMasterKey[LoginTicketAuthority::kMasterKeySize] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                                                                    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };

        static LoginCredential makeCredential( const utf8* pName, const utf8* pPassword )
        {
            LoginCredential credential;
            credential._loginName = pName;
            credential._password  = pPassword;
            return credential;
        }

        static int32 countEvents( const vector<LoginEvent>& listEvent, LoginEvent::Kind kind )
        {
            int32 count = 0;
            for ( const LoginEvent& event : listEvent )
                count += event._kind == kind ? 1 : 0;
            return count;
        }
    };

    /** @brief 서버 한 대 — 앞(MemoryServiceStore) + 서비스. 데이터는 밖에서 빌린다. 동기 도우미는 맡기고 거둔 뒤 완료를 돌려준다(메모리 구현은 맡기는 자리에서 돈다). */
    struct LoginNode
    {
        MemoryServiceStore       _store;
        TestLoginCrypto          _crypto;
        unique_ptr<LoginService> _service;
        uint64                   _nextTag;

        LoginNode( MemoryServiceDatabase* pDatabase, uint64 seed, const LoginSettings& settings )
            : _store{ pDatabase }
            , _crypto{ seed }
            , _service{}
            , _nextTag{ 1 }
        {
            restart( settings );
        }

        ~LoginNode()
        {
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        /** @brief 프로세스가 죽고 다시 떴다 — 서비스만 새로 만든다(저장소 데이터는 그대로). */
        void restart( const LoginSettings& settings )
        {
            (void)_store.pollCompletions();
            _service = make_unique<LoginService>();
            _service->initialize( &_store, &_crypto, settings, TestLoginServiceInternal::kMasterKey );
        }

        LoginCompletion settle()
        {
            (void)_store.pollCompletions();
            vector<LoginCompletion> listCompletion;
            _service->drainCompletions( listCompletion );
            return listCompletion.empty() ? LoginCompletion{} : listCompletion.back();
        }

        LoginResult registerAccount( const LoginCredential& credential, int64 nowMs, uint64* pOutAccountId = nullptr )
        {
            _service->registerAccount( credential, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            if ( pOutAccountId != nullptr )
                *pOutAccountId = completion._identity._accountId;
            return completion._result;
        }

        LoginResult login( const LoginCredential& credential, uint64 clientKey, int64 nowMs, LoginGrant& outGrant )
        {
            _service->login( credential, clientKey, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outGrant                         = completion._grant;
            return completion._result;
        }

        LoginResult resumeSession( const LoginSessionToken& token, uint64 clientKey, int64 nowMs, LoginGrant& outGrant )
        {
            _service->resumeSession( token, clientKey, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outGrant                         = completion._grant;
            return completion._result;
        }

        LoginResult validateSession( const LoginSessionToken& token, int64 nowMs, AccountIdentity& outIdentity, LoginRevokeReason* pOutRevokeReason = nullptr )
        {
            _service->validateSession( token, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outIdentity                      = completion._identity;
            if ( pOutRevokeReason != nullptr )
                *pOutRevokeReason = completion._grant._revokeReason;
            return completion._result;
        }

        LoginResult logout( const LoginSessionToken& token, int64 nowMs )
        {
            _service->logout( token, nowMs, _nextTag++ );
            return settle()._result;
        }

        LoginResult issueGameTicket( const LoginSessionToken& token, const hashed_string& serverId, int64 nowMs, NetGameTicket& outTicket )
        {
            _service->issueGameTicket( token, serverId, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outTicket                        = completion._ticket;
            return completion._result;
        }

        LoginResult revokeAccountSessions( uint64 accountId, int64 nowMs )
        {
            _service->revokeAccountSessions( accountId, nowMs, _nextTag++ );
            return settle()._result;
        }

        void markDisconnected( uint64 sessionId, int64 nowMs )
        {
            _service->markDisconnected( sessionId, nowMs );
            (void)settle();
        }

        void refreshOnlineSessions( int64 nowMs, int32 maxCount )
        {
            _service->refreshOnlineSessions( nowMs, maxCount );
            (void)settle();
        }

        bool  isAccountOnline( uint64 accountId ) const { return _service->isAccountOnline( accountId ); }
        int32 getOnlineCount() const { return _service->getOnlineCount(); }
        void  drainEvents( vector<LoginEvent>& outListEvent ) { _service->drainEvents( outListEvent ); }
        bool  findIdentityByDisplayName( string_view name, AccountIdentity& outIdentity ) const { return _service->findIdentityByDisplayName( name, outIdentity ); }
        bool  findIdentity( uint64 accountId, AccountIdentity& outIdentity ) const { return _service->findIdentity( accountId, outIdentity ); }
    };

    /** @brief 데이터 하나 + 서버 한 대(재시작은 서비스만 새로 만든다). */
    struct LoginFixture
    {
        MemoryServiceDatabase _database;
        LoginSettings         _settings;
        LoginNode             _node;

        LoginFixture()
            : _database{}
            , _settings{}
            , _node{ &_database, 7u, _settings }
        {
        }

        void       restart() { _node.restart( _settings ); }
        LoginNode* operator->() { return &_node; }
    };
} // namespace

SW_TEST_CASE( LoginServiceTest, RegisterLoginAndValidate )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    uint64       accountId = 0;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "Alice_01", "correct horse" ), 0, &accountId ) == LoginResult::Ok );
    SW_EXPECT_TRUE( fixture->registerAccount( Internal::makeCredential( "alice_01", "other password" ), 0 ) == LoginResult::NameTaken ); // 대소문자 무시
    SW_EXPECT_TRUE( fixture->registerAccount( Internal::makeCredential( "a!", "correct horse" ), 0 ) == LoginResult::InvalidName );
    SW_EXPECT_TRUE( fixture->registerAccount( Internal::makeCredential( "bob", "short" ), 0 ) == LoginResult::InvalidPassword );

    AccountIdentity identity;
    SW_EXPECT_FALSE( fixture->findIdentity( accountId, identity ) ); // 디렉터리는 붙어 있는 계정만

    LoginGrant grant;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "ALICE_01", "correct horse" ), 1, 1000, grant ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( accountId, grant._identity._accountId );
    SW_EXPECT_EQUAL( string( "Alice_01" ), grant._identity._displayName ); // 가입 때 친 글자 그대로
    SW_EXPECT_TRUE( fixture->isAccountOnline( accountId ) );

    SW_EXPECT_TRUE( fixture->validateSession( grant._token, 2000, identity ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( accountId, identity._accountId );
    SW_EXPECT_TRUE( fixture->findIdentityByDisplayName( "alice_01", identity ) );
    SW_EXPECT_TRUE( fixture->findIdentity( accountId, identity ) );

    LoginSessionToken tampered = grant._token;
    tampered._arrSecret[5] ^= 0x01;
    SW_EXPECT_TRUE( fixture->validateSession( tampered, 2000, identity ) == LoginResult::InvalidToken );
    LoginSessionToken unknown = grant._token;
    unknown._sessionId ^= 0x10;
    SW_EXPECT_TRUE( fixture->validateSession( unknown, 2000, identity ) == LoginResult::InvalidToken );

    uint8 arrWire[LoginConstant::kTokenWireSize];
    grant._token.writeBytes( arrWire );
    LoginSessionToken readBack;
    SW_ASSERT_TRUE( readBack.readBytes( arrWire, LoginConstant::kTokenWireSize ) );
    SW_EXPECT_TRUE( fixture->validateSession( readBack, 2000, identity ) == LoginResult::Ok );
    SW_EXPECT_FALSE( readBack.readBytes( arrWire, LoginConstant::kTokenWireSize - 1 ) );

    SW_EXPECT_TRUE( fixture->logout( grant._token, 3000 ) == LoginResult::Ok );
    SW_EXPECT_FALSE( fixture->isAccountOnline( accountId ) );
    SW_EXPECT_TRUE( fixture->validateSession( grant._token, 3000, identity ) == LoginResult::InvalidToken );
}

SW_TEST_CASE( LoginServiceTest, WrongPasswordLocksAndUnknownAccountLooksTheSame )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "carol", "password123" ), 0 ) == LoginResult::Ok );
    LoginGrant  grant;
    const int32 hashCountBefore = fixture->_crypto._passwordHashCount;
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "nobody", "password123" ), 1, 0, grant ) == LoginResult::WrongCredentials );
    SW_EXPECT_EQUAL( hashCountBefore + 1, fixture->_crypto._passwordHashCount ); // 없는 계정도 해시를 한 번 돌린다
    for ( int32 attempt = 0; attempt < 4; ++attempt )
        SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "carol", "wrong-pass" ), 2 + static_cast<uint64>( attempt ), 100, grant ) == LoginResult::WrongCredentials );
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "carol", "wrong-pass" ), 9, 100, grant ) == LoginResult::AccountLocked );
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "carol", "password123" ), 10, 200, grant ) == LoginResult::AccountLocked ); // 맞아도 잠금 동안은
    SW_EXPECT_EQUAL( fixture._settings._lockoutMs - 100, grant._retryAfterMs );
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "carol", "password123" ), 11, 100 + fixture._settings._lockoutMs, grant ) == LoginResult::Ok );
}

SW_TEST_CASE( LoginServiceTest, AttemptsAreRateLimitedPerClient )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    LoginGrant   grant;
    for ( int32 attempt = 0; attempt < fixture._settings._attemptBurst; ++attempt )
        SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "ghost", "whatever1" ), 77, 0, grant ) == LoginResult::WrongCredentials );
    const int32 hashCountBefore = fixture->_crypto._passwordHashCount;
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "ghost", "whatever1" ), 77, 0, grant ) == LoginResult::RateLimited );
    SW_EXPECT_EQUAL( fixture._settings._attemptRefillMs, grant._retryAfterMs );
    SW_EXPECT_EQUAL( hashCountBefore, fixture->_crypto._passwordHashCount );                                                             // 맡기지도 않았다
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "ghost", "whatever1" ), 78, 0, grant ) == LoginResult::WrongCredentials ); // 다른 주소는 따로
}

SW_TEST_CASE( LoginServiceTest, DuplicateLoginKicksTheOldSessionOrIsRejected )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    uint64       accountId = 0;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "dave", "password123" ), 0, &accountId ) == LoginResult::Ok );
    LoginGrant first;
    LoginGrant second;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "dave", "password123" ), 1, 0, first ) == LoginResult::Ok );
    vector<LoginEvent> listEvent;
    fixture->drainEvents( listEvent );
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "dave", "password123" ), 2, 10, second ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( first._token._sessionId, second._replacedSessionId );
    listEvent.clear();
    fixture->drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, LoginEvent::Kind::Revoked ) ); // 이 서버에 붙어 있던 옛 세션 — 바인딩이 그 연결을 닫는다
    AccountIdentity   identity;
    LoginRevokeReason reason = LoginRevokeReason::None;
    SW_EXPECT_TRUE( fixture->validateSession( first._token, 20, identity, &reason ) == LoginResult::Revoked );
    SW_EXPECT_TRUE( reason == LoginRevokeReason::DuplicateLogin );
    SW_EXPECT_TRUE( fixture->validateSession( second._token, 20, identity ) == LoginResult::Ok );

    fixture._settings._duplicatePolicy = LoginDuplicatePolicy::RejectNew;
    fixture.restart();
    LoginGrant third;
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "dave", "password123" ), 3, 30, third ) == LoginResult::AlreadyLoggedIn ); // second 가 붙어 있다
    fixture->markDisconnected( second._token._sessionId, 40 );
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "dave", "password123" ), 3, 50, third ) == LoginResult::Ok ); // 끊긴 세션은 밀어낸다
}

SW_TEST_CASE( LoginServiceTest, ResumeWithinGraceRotatesTheTokenAndExpiresAfter )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "erin", "password123" ), 0 ) == LoginResult::Ok );
    LoginGrant grant;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "erin", "password123" ), 1, 0, grant ) == LoginResult::Ok );
    vector<LoginEvent> listEvent;
    fixture->drainEvents( listEvent );
    listEvent.clear();
    fixture->markDisconnected( grant._token._sessionId, 1000 );
    fixture->drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, LoginEvent::Kind::Disconnected ) );
    SW_EXPECT_FALSE( fixture->isAccountOnline( grant._identity._accountId ) );
    AccountIdentity identity;
    SW_EXPECT_FALSE( fixture->findIdentityByDisplayName( "erin", identity ) ); // 오프라인은 디렉터리에 없다

    LoginGrant resumed;
    SW_ASSERT_TRUE( fixture->resumeSession( grant._token, 1, 1000 + fixture._settings._reconnectGraceMs - 1, resumed ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( grant._token._sessionId, resumed._token._sessionId ); // 같은 세션
    SW_EXPECT_EQUAL( string( "erin" ), resumed._identity._displayName );
    SW_EXPECT_TRUE( fixture->validateSession( grant._token, 5000, identity ) == LoginResult::InvalidToken ); // 옛 비밀은 죽었다
    SW_EXPECT_TRUE( fixture->validateSession( resumed._token, 5000, identity ) == LoginResult::Ok );
    SW_EXPECT_TRUE( fixture->isAccountOnline( grant._identity._accountId ) );

    fixture->markDisconnected( resumed._token._sessionId, 10000 );
    LoginGrant late;
    SW_EXPECT_TRUE( fixture->resumeSession( resumed._token, 1, 10000 + fixture._settings._reconnectGraceMs, late ) == LoginResult::Expired );
    SW_EXPECT_TRUE( fixture->validateSession( resumed._token, fixture._settings._sessionLifetimeMs, identity ) == LoginResult::Expired );
}

SW_TEST_CASE( LoginServiceTest, SessionsSurviveAServiceRestart )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "frank", "password123" ), 0 ) == LoginResult::Ok );
    LoginGrant grant;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "frank", "password123" ), 1, 0, grant ) == LoginResult::Ok );

    fixture.restart(); // 프로세스가 죽었다 — markDisconnected 도 못 불렸다(저장소에는 "붙어 있음")
    SW_EXPECT_EQUAL( 0, fixture->getOnlineCount() );
    LoginGrant resumed;
    SW_ASSERT_TRUE( fixture->resumeSession( grant._token, 1, 5000, resumed ) == LoginResult::Ok ); // 토큰이 있으면 넘겨받는다
    SW_EXPECT_TRUE( fixture->isAccountOnline( grant._identity._accountId ) );
}

SW_TEST_CASE( LoginServiceTest, TwoServersLoggingInTheSameAccountLastOneWins )
{
    using Internal = TestLoginServiceInternal;
    MemoryServiceDatabase database;
    LoginSettings         settings;
    LoginNode             serverA{ &database, 11u, settings };
    LoginNode             serverB{ &database, 12u, settings };
    SW_ASSERT_TRUE( serverA.registerAccount( Internal::makeCredential( "gina", "password123" ), 0 ) == LoginResult::Ok );

    LoginGrant onA;
    LoginGrant onB;
    SW_ASSERT_TRUE( serverA.login( Internal::makeCredential( "gina", "password123" ), 1, 0, onA ) == LoginResult::Ok );
    SW_ASSERT_TRUE( serverB.login( Internal::makeCredential( "gina", "password123" ), 2, 10, onB ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( onA._token._sessionId, onB._replacedSessionId );
    SW_EXPECT_TRUE( serverA.isAccountOnline( onA._identity._accountId ) ); // A 는 아직 모른다

    vector<LoginEvent> listEvent;
    serverA.drainEvents( listEvent );
    listEvent.clear();
    serverA.refreshOnlineSessions( 20, 16 );
    serverA.drainEvents( listEvent );
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == LoginEvent::Kind::Revoked && listEvent[0]._reason == LoginRevokeReason::DuplicateLogin );
    SW_EXPECT_FALSE( serverA.isAccountOnline( onA._identity._accountId ) );
    SW_EXPECT_TRUE( serverB.isAccountOnline( onB._identity._accountId ) );
}

SW_TEST_CASE( LoginServiceTest, AdministrativeRevokeEndsTheSessionAndTellsTheBinding )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    uint64       accountId = 0;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "kate", "password123" ), 0, &accountId ) == LoginResult::Ok );
    LoginGrant grant;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "kate", "password123" ), 1, 0, grant ) == LoginResult::Ok );
    vector<LoginEvent> listEvent;
    fixture->drainEvents( listEvent );
    listEvent.clear();
    SW_EXPECT_TRUE( fixture->revokeAccountSessions( accountId, 100 ) == LoginResult::Ok );
    fixture->drainEvents( listEvent );
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == LoginEvent::Kind::Revoked && listEvent[0]._reason == LoginRevokeReason::Administrative );
    SW_EXPECT_FALSE( fixture->isAccountOnline( accountId ) );
    AccountIdentity   identity;
    LoginRevokeReason reason = LoginRevokeReason::None;
    SW_EXPECT_TRUE( fixture->validateSession( grant._token, 200, identity, &reason ) == LoginResult::Revoked );
    SW_EXPECT_TRUE( reason == LoginRevokeReason::Administrative );
    SW_EXPECT_TRUE( fixture->revokeAccountSessions( accountId, 300 ) == LoginResult::Ok ); // 세션이 없어도 된다
}

SW_TEST_CASE( LoginServiceTest, GameTicketIsSignedBoundToServerAndExpires )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "hank", "password123" ), 0 ) == LoginResult::Ok );
    LoginGrant grant;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "hank", "password123" ), 1, 0, grant ) == LoginResult::Ok );
    NetGameTicket ticket;
    SW_ASSERT_TRUE( fixture->issueGameTicket( grant._token, "zone-1", 100, ticket ) == LoginResult::Ok );

    // 게임 서버 — 같은 주 키만 있고 저장소는 없다.
    TestLoginCrypto      gameCrypto{ 99u };
    LoginTicketAuthority gameAuthority;
    gameAuthority.initialize( &gameCrypto, Internal::kMasterKey );
    NetGameTicketClaim claim;
    SW_ASSERT_TRUE( gameAuthority.verifyTicket( ticket._arrToken, NetGameTicket::kTokenSize, "zone-1", 200, claim ) );
    SW_EXPECT_EQUAL( grant._identity._accountId, claim._accountId );
    SW_EXPECT_EQUAL( grant._token._sessionId, claim._sessionId );
    SW_EXPECT_TRUE( std::equal( claim._arrSecret, claim._arrSecret + NetGameTicket::kSecretSize, ticket._arrSecret ) ); // 양쪽이 같은 비밀 → 같은 UDP 키
    SW_EXPECT_TRUE( gameAuthority.verifyTicket( ticket._arrToken, NetGameTicket::kTokenSize, "zone-1", 300, claim ) );  // 시한 안에서는 다시(UDP 재접속)
    SW_EXPECT_TRUE( gameAuthority.verifyTicket( ticket._arrToken, NetGameTicket::kTokenSize, "ZONE-1", 300, claim ) );  // 서버 id 는 대소문자 무시

    SW_EXPECT_FALSE( gameAuthority.verifyTicket( ticket._arrToken, NetGameTicket::kTokenSize, "zone-2", 200, claim ) );                 // 다른 서버
    SW_EXPECT_FALSE( gameAuthority.verifyTicket( ticket._arrToken, NetGameTicket::kTokenSize, "zone-1", ticket._expiresAtMs, claim ) ); // 시한
    SW_EXPECT_FALSE( gameAuthority.verifyTicket( ticket._arrToken, NetGameTicket::kTokenSize - 1, "zone-1", 200, claim ) );             // 크기
    NetGameTicket forged = ticket;
    forged._arrToken[0] ^= 0x01; // 계정 id 를 바꿔 본다
    SW_EXPECT_FALSE( gameAuthority.verifyTicket( forged._arrToken, NetGameTicket::kTokenSize, "zone-1", 200, claim ) );
    const uint8          arrOtherKey[LoginTicketAuthority::kMasterKeySize]{};
    LoginTicketAuthority wrongKey;
    wrongKey.initialize( &gameCrypto, arrOtherKey );
    SW_EXPECT_FALSE( wrongKey.verifyTicket( ticket._arrToken, NetGameTicket::kTokenSize, "zone-1", 200, claim ) );

    fixture->markDisconnected( grant._token._sessionId, 400 );
    NetGameTicket afterExpiry;
    SW_EXPECT_TRUE( fixture->issueGameTicket( grant._token, "zone-1", 400 + fixture._settings._reconnectGraceMs, afterExpiry ) == LoginResult::Expired );
}

SW_TEST_CASE( LoginServiceTest, StoreFailureNeverLeavesAHalfSession )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "iris", "password123" ), 0 ) == LoginResult::Ok );
    const uint64 hashBefore = fixture._database.computeContentHash();
    LoginGrant   grant;
    fixture._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "iris", "password123" ), 1, 0, grant ) == LoginResult::StoreUnavailable );
    SW_EXPECT_EQUAL( hashBefore, fixture._database.computeContentHash() ); // 세션도 링크도 없다
    SW_EXPECT_EQUAL( 0, fixture->getOnlineCount() );

    fixture._database.armFault( ServiceStoreFault::LoseCommitReply ); // 세션은 생겼지만 클라이언트는 모른다
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "iris", "password123" ), 1, 10, grant ) == LoginResult::StoreUnavailable );
    SW_EXPECT_EQUAL( 0, fixture->getOnlineCount() );
    LoginGrant retry;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "iris", "password123" ), 1, 20, retry ) == LoginResult::Ok );
    SW_EXPECT_TRUE( retry._replacedSessionId != 0 ); // 유령 세션을 밀어냈다
    AccountIdentity identity;
    SW_EXPECT_TRUE( fixture->validateSession( retry._token, 30, identity ) == LoginResult::Ok );
}

SW_TEST_CASE( LoginServiceTest, ChangedHashParamsRehashOnNextLogin )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "jack", "password123" ), 0 ) == LoginResult::Ok );
    fixture._settings._passwordHashParams._iterationCount = 3;
    fixture.restart();
    LoginGrant  grant;
    const int32 hashCountBefore = fixture->_crypto._passwordHashCount;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "jack", "password123" ), 1, 0, grant ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( hashCountBefore + 2, fixture->_crypto._passwordHashCount ); // 옛 매개변수로 확인 + 새 매개변수로 다시
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "jack", "password123" ), 1, 10, grant ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( hashCountBefore + 3, fixture->_crypto._passwordHashCount ); // 이제 한 번
}

SW_TEST_CASE( LoginServiceTest, RealCryptoHashesWithArgon2idAndSignsTickets )
{
    using Internal = TestLoginServiceInternal;
    NetSecurityLoginCrypto crypto{ &EngineNetSecurity::getProvider() };
    LoginSettings          settings; // 기본 Argon2id 매개변수(OWASP 19 MiB · 2 회 · 1 레인)
    SW_ASSERT_TRUE( crypto.isPasswordHashSupported( settings._passwordHashParams ) );

    MemoryServiceDatabase database;
    MemoryServiceStore    store{ &database };
    LoginService          service;
    service.initialize( &store, &crypto, settings, Internal::kMasterKey );
    vector<LoginCompletion> listCompletion;

    service.registerAccount( Internal::makeCredential( "liam", "password123" ), 0, 1 );
    service.login( Internal::makeCredential( "liam", "wrong-pass" ), 1, 10, 2 );
    service.login( Internal::makeCredential( "liam", "password123" ), 1, 20, 3 );
    (void)store.pollCompletions();
    service.drainCompletions( listCompletion );
    SW_ASSERT_EQUAL( size_t( 3 ), listCompletion.size() );
    SW_EXPECT_TRUE( listCompletion[0]._result == LoginResult::Ok );
    SW_EXPECT_TRUE( listCompletion[1]._result == LoginResult::WrongCredentials );
    SW_ASSERT_TRUE( listCompletion[2]._result == LoginResult::Ok );
    const LoginSessionToken token = listCompletion[2]._grant._token;

    listCompletion.clear();
    service.issueGameTicket( token, "zone-1", 30, 4 );
    (void)store.pollCompletions();
    service.drainCompletions( listCompletion );
    SW_ASSERT_EQUAL( size_t( 1 ), listCompletion.size() );
    SW_ASSERT_TRUE( listCompletion[0]._result == LoginResult::Ok );
    NetSecurityLoginCrypto gameCrypto{ &EngineNetSecurity::getProvider() };
    LoginTicketAuthority   gameAuthority;
    gameAuthority.initialize( &gameCrypto, Internal::kMasterKey );
    NetGameTicketClaim claim;
    SW_EXPECT_TRUE( gameAuthority.verifyTicket( listCompletion[0]._ticket._arrToken, NetGameTicket::kTokenSize, "zone-1", 40, claim ) );
    SW_EXPECT_EQUAL( token._sessionId, claim._sessionId );

    store.shutdown();
    (void)store.pollCompletions();
    service.shutdown();
}
