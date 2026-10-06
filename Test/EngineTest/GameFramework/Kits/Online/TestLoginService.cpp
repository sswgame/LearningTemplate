#include "pch.h"

#include "Core/Common/HashUtil.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Online/Server/Account/LoginService.h"
#include "GameFramework/Kits/Online/Server/Account/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Online/Server/Account/Platform/FakePlatformLoginProvider.h"

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
            _service->login( credential, AccountClientInfo{}, clientKey, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outGrant                         = completion._grant;
            return completion._result;
        }

        LoginResult resumeSession( const LoginSessionToken& token, uint64 clientKey, int64 nowMs, LoginGrant& outGrant )
        {
            _service->resumeSession( token, AccountClientInfo{}, clientKey, nowMs, _nextTag++ );
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
            _service->revokeAccountSessions( accountId, LoginRevokeReason::Administrative, nowMs, _nextTag++ );
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

        LoginResult guestLogin( uint8 secretSeed, int64 nowMs, LoginGrant& outGrant, const AccountClientInfo& clientInfo = AccountClientInfo{} )
        {
            uint8 arrSecret[LoginConstant::kDeviceSecretSize];
            for ( int32 byteIndex = 0; byteIndex < LoginConstant::kDeviceSecretSize; ++byteIndex )
                arrSecret[byteIndex] = static_cast<uint8>( secretSeed + byteIndex );
            _service->guestLogin( arrSecret, clientInfo, secretSeed, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outGrant                         = completion._grant;
            return completion._result;
        }

        /** @brief 외부 로그인 — 제공자 확인은 다음 `tick` 에 끝나고 저장 일이 그 자리에서 돈다. */
        LoginResult platformLogin( string_view provider, string_view ticketText, int64 nowMs, LoginGrant& outGrant )
        {
            const vector<uint8> ticket( ticketText.begin(), ticketText.end() );
            _service->platformLogin( provider, ticket, AccountClientInfo{}, 1, nowMs, _nextTag++ );
            _service->tick( nowMs );
            const LoginCompletion completion = settle();
            outGrant                         = completion._grant;
            return completion._result;
        }

        LoginResult linkCredential( const LoginSessionToken& token, const LoginCredential& credential, int64 nowMs, AccountIdentity& outIdentity )
        {
            _service->linkCredential( token, credential, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outIdentity                      = completion._identity;
            return completion._result;
        }

        LoginResult linkPlatform( const LoginSessionToken& token, string_view provider, string_view ticketText, int64 nowMs )
        {
            const vector<uint8> ticket( ticketText.begin(), ticketText.end() );
            _service->linkPlatform( token, provider, ticket, nowMs, _nextTag++ );
            _service->tick( nowMs );
            return settle()._result;
        }

        LoginResult unlinkPlatform( const LoginSessionToken& token, string_view provider, int64 nowMs )
        {
            _service->unlinkPlatform( token, provider, nowMs, _nextTag++ );
            return settle()._result;
        }

        LoginResult listLinks( const LoginSessionToken& token, int64 nowMs, AccountLinkSummary& outSummary )
        {
            _service->listLinks( token, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outSummary                       = completion._linkSummary;
            return completion._result;
        }

        LoginResult requestDeletion( const LoginSessionToken& token, int64 nowMs, int64& outDueMs )
        {
            _service->requestDeletion( token, nowMs, _nextTag++ );
            const LoginCompletion completion = settle();
            outDueMs                         = completion._grant._deletionDueMs;
            return completion._result;
        }

        LoginResult cancelDeletion( const LoginSessionToken& token, int64 nowMs )
        {
            _service->cancelDeletion( token, nowMs, _nextTag++ );
            return settle()._result;
        }

        void purgeDueDeletions( int64 nowMs )
        {
            _service->purgeDueDeletions( nowMs, 16 );
            (void)settle();
        }

        LoginResult revokeAccountSessions( uint64 accountId, LoginRevokeReason reason, int64 nowMs )
        {
            _service->revokeAccountSessions( accountId, reason, nowMs, _nextTag++ );
            return settle()._result;
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

    struct TestLoginServiceGuestInternal
    {
        static ServiceAuditEntry makeAudit()
        {
            ServiceAuditEntry audit;
            audit._actor   = "gm.0000000000000001";
            audit._action  = "config.set";
            audit._subject = "config";
            audit._timeMs  = 1;
            return audit;
        }

        static RemoteConfigValue makeText( const utf8* pText )
        {
            RemoteConfigValue value;
            value._type = RemoteConfigValueType::Text;
            value._text = pText;
            return value;
        }

        static void writeSanction( MemoryServiceDatabase& database, uint64 accountId, ServiceSanctionKind kind, int64 untilMs )
        {
            ServiceSanctionState state;
            (void)ServiceSanction::readState( database, accountId, state );
            state._arrUntilMs[static_cast<int32>( kind )] = untilMs;
            state._reasonCode                             = "sanction.cheat";
            ServiceTransaction transaction;
            ServiceSanction::stageWrite( transaction, accountId, state );
            (void)database.commit( transaction );
        }
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
    service.login( Internal::makeCredential( "liam", "wrong-pass" ), AccountClientInfo{}, 1, 10, 2 );
    service.login( Internal::makeCredential( "liam", "password123" ), AccountClientInfo{}, 1, 20, 3 );
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

SW_TEST_CASE( LoginServiceTest, GuestLoginIsStablePerDevice )
{
    LoginFixture fixture;
    LoginGrant   first;
    SW_ASSERT_TRUE( fixture->guestLogin( 1, 0, first ) == LoginResult::Ok );
    SW_EXPECT_TRUE( first._bCreated == SW_TRUE );
    SW_EXPECT_TRUE( first._identity._bGuest == SW_TRUE );
    SW_EXPECT_TRUE( first._identity._displayName.rfind( "Guest-", 0 ) == 0 );
    LoginGrant again;
    SW_ASSERT_TRUE( fixture->guestLogin( 1, 100, again ) == LoginResult::Ok ); // 같은 장치 → 같은 계정
    SW_EXPECT_TRUE( again._bCreated == SW_FALSE );
    SW_EXPECT_EQUAL( first._identity._accountId, again._identity._accountId );
    SW_EXPECT_EQUAL( first._token._sessionId, again._replacedSessionId ); // 새 로그인이 옛 세션을 밀어낸다
    LoginGrant other;
    SW_ASSERT_TRUE( fixture->guestLogin( 2, 200, other ) == LoginResult::Ok ); // 다른 장치 → 다른 계정
    SW_EXPECT_NOT_EQUAL( first._identity._accountId, other._identity._accountId );
    SW_EXPECT_EQUAL( 2, fixture._database.countRecords( hashed_string( "account_guest" ) ) ); // 비밀이 아니라 다이제스트가 키
}

SW_TEST_CASE( LoginServiceTest, LinkingKeepsTheAccountAndClearsGuest )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    LoginGrant   guest;
    SW_ASSERT_TRUE( fixture->guestLogin( 3, 0, guest ) == LoginResult::Ok );
    AccountIdentity linked;
    SW_ASSERT_TRUE( fixture->linkCredential( guest._token, Internal::makeCredential( "Mina", "password123" ), 10, linked ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( guest._identity._accountId, linked._accountId );
    SW_EXPECT_TRUE( linked._bGuest == SW_FALSE );
    SW_EXPECT_EQUAL( string( "Mina" ), linked._displayName );
    AccountIdentity online;
    SW_EXPECT_TRUE( fixture->findIdentityByDisplayName( "mina", online ) ); // 디렉터리 이름 색인도 바뀐다

    LoginGrant byName;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "mina", "password123" ), 1, 20, byName ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( guest._identity._accountId, byName._identity._accountId );
    LoginGrant byDevice; // 연동 뒤에도 같은 장치는 비밀번호 없이(기본 설정)
    SW_ASSERT_TRUE( fixture->guestLogin( 3, 30, byDevice ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( guest._identity._accountId, byDevice._identity._accountId );
    SW_EXPECT_TRUE( byDevice._identity._bGuest == SW_FALSE );

    AccountIdentity again;
    SW_EXPECT_TRUE( fixture->linkCredential( byDevice._token, Internal::makeCredential( "mina2", "password123" ), 40, again ) == LoginResult::AlreadyLinked ); // 이름은 하나
}

SW_TEST_CASE( LoginServiceTest, LinkingAnAlreadyUsedNameOrSubjectFails )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture              fixture;
    FakePlatformLoginProvider provider{ "fake" };
    SW_ASSERT_TRUE( fixture->_service->registerPlatformProvider( &provider ) );
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "taken", "password123" ), 0 ) == LoginResult::Ok );
    LoginGrant other;
    SW_ASSERT_TRUE( fixture->platformLogin( "fake", "subject:s-1", 0, other ) == LoginResult::Ok );
    LoginGrant guest;
    SW_ASSERT_TRUE( fixture->guestLogin( 4, 0, guest ) == LoginResult::Ok );

    const uint64    before = fixture._database.computeContentHash();
    AccountIdentity identity;
    SW_EXPECT_TRUE( fixture->linkCredential( guest._token, Internal::makeCredential( "TAKEN", "password123" ), 10, identity ) == LoginResult::AlreadyLinked );
    SW_EXPECT_TRUE( fixture->linkPlatform( guest._token, "fake", "subject:s-1", 10 ) == LoginResult::AlreadyLinked ); // 다른 계정 것 — 합치지 않는다
    SW_EXPECT_EQUAL( before, fixture._database.computeContentHash() );                                                // 아무것도 안 바뀜
    SW_EXPECT_TRUE( fixture->linkPlatform( guest._token, "fake", "reject", 10 ) == LoginResult::ProviderRejected );
    SW_EXPECT_TRUE( fixture->linkPlatform( guest._token, "none", "subject:x", 10 ) == LoginResult::ProviderUnavailable );
}

SW_TEST_CASE( LoginServiceTest, PlatformLoginCreatesThenReuses )
{
    LoginFixture              fixture;
    FakePlatformLoginProvider provider{ "fake" };
    SW_ASSERT_TRUE( fixture->_service->registerPlatformProvider( &provider ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a second provider with the same name is refused with an error log" );
        SW_EXPECT_FALSE( fixture->_service->registerPlatformProvider( &provider ) ); // 같은 이름 둘
    }

    LoginGrant first;
    SW_ASSERT_TRUE( fixture->platformLogin( "fake", "subject:u-77:Hero Name!", 0, first ) == LoginResult::Ok );
    SW_EXPECT_TRUE( first._bCreated == SW_TRUE );
    SW_EXPECT_TRUE( first._identity._bGuest == SW_FALSE );
    SW_EXPECT_TRUE( first._identity._displayName.rfind( "HeroName-", 0 ) == 0 ); // 제공자 이름의 ASCII 영숫자 + id 꼬리
    LoginGrant again;
    SW_ASSERT_TRUE( fixture->platformLogin( "fake", "subject:u-77", 100, again ) == LoginResult::Ok );
    SW_EXPECT_TRUE( again._bCreated == SW_FALSE );
    SW_EXPECT_EQUAL( first._identity._accountId, again._identity._accountId );

    LoginGrant failed;
    SW_EXPECT_TRUE( fixture->platformLogin( "fake", "reject", 200, failed ) == LoginResult::ProviderRejected );
    SW_EXPECT_TRUE( fixture->platformLogin( "fake", "down", 200, failed ) == LoginResult::ProviderUnavailable );
    SW_EXPECT_TRUE( fixture->platformLogin( "steam", "subject:u-77", 200, failed ) == LoginResult::ProviderUnavailable ); // 올리지 않은 제공자
    SW_EXPECT_EQUAL( 4, provider.getSubmittedCount() );                                                                   // 없는 제공자는 맡기지도 않는다
}

SW_TEST_CASE( LoginServiceTest, MultipleLinksAndTheLastMethodCannotBeUnlinked )
{
    LoginFixture              fixture;
    FakePlatformLoginProvider google{ "google" };
    FakePlatformLoginProvider kakao{ "kakao" };
    SW_ASSERT_TRUE( fixture->_service->registerPlatformProvider( &google ) );
    SW_ASSERT_TRUE( fixture->_service->registerPlatformProvider( &kakao ) );
    LoginGrant grant;
    SW_ASSERT_TRUE( fixture->platformLogin( "google", "subject:g-1", 0, grant ) == LoginResult::Ok );
    SW_ASSERT_TRUE( fixture->linkPlatform( grant._token, "kakao", "subject:k-1", 10 ) == LoginResult::Ok );
    SW_EXPECT_TRUE( fixture->linkPlatform( grant._token, "kakao", "subject:k-2", 10 ) == LoginResult::AlreadyLinked ); // 제공자마다 하나

    AccountLinkSummary summary;
    SW_ASSERT_TRUE( fixture->listLinks( grant._token, 20, summary ) == LoginResult::Ok );
    SW_ASSERT_EQUAL( size_t( 2 ), summary._listProvider.size() );
    SW_EXPECT_EQUAL( string( "google" ), summary._listProvider[0] );
    SW_EXPECT_EQUAL( string( "kakao" ), summary._listProvider[1] );
    SW_EXPECT_EQUAL( 2, summary.getLoginMethodCount() );

    LoginGrant viaKakao; // 둘 다 같은 계정으로 들어온다
    SW_ASSERT_TRUE( fixture->platformLogin( "kakao", "subject:k-1", 30, viaKakao ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( grant._identity._accountId, viaKakao._identity._accountId );

    SW_EXPECT_TRUE( fixture->unlinkPlatform( viaKakao._token, "naver", 40 ) == LoginResult::NotLinked );
    SW_ASSERT_TRUE( fixture->unlinkPlatform( viaKakao._token, "google", 40 ) == LoginResult::Ok );
    SW_EXPECT_TRUE( fixture->unlinkPlatform( viaKakao._token, "kakao", 50 ) == LoginResult::LastLoginMethod );
    LoginGrant newAccount; // 풀린 주체는 이제 새 계정이 된다
    SW_ASSERT_TRUE( fixture->platformLogin( "google", "subject:g-1", 60, newAccount ) == LoginResult::Ok );
    SW_EXPECT_TRUE( newAccount._bCreated == SW_TRUE );
    SW_EXPECT_NOT_EQUAL( grant._identity._accountId, newAccount._identity._accountId );
}

SW_TEST_CASE( LoginServiceTest, OldBuildIsToldToUpdate )
{
    using Internal = TestLoginServiceGuestInternal;
    LoginFixture fixture;
    RemoteConfig config;
    config.submitSet( fixture->_store, nullptr, "account.minimum_build.windows", Internal::makeText( "1.9.0" ), Internal::makeAudit() );
    config.submitSet( fixture->_store, nullptr, "account.recommended_build.windows", Internal::makeText( "1.10.0" ), Internal::makeAudit() );
    config.submitSet( fixture->_store, nullptr, "account.store_url.windows", Internal::makeText( "https://store.example/game" ), Internal::makeAudit() );
    (void)fixture->_store.pollCompletions();
    fixture->_service->setRemoteConfig( &config );

    SW_EXPECT_EQUAL( 1, AccountUtil::compareBuild( "1.10.0", "1.9.9" ) );
    SW_EXPECT_EQUAL( 0, AccountUtil::compareBuild( "1.2", "1.2.0" ) );
    SW_EXPECT_EQUAL( -1, AccountUtil::compareBuild( "1.2.0-rc", "1.2.1" ) );

    AccountClientInfo oldClient;
    oldClient._build    = "1.8.5";
    oldClient._platform = "windows";
    LoginGrant grant;
    SW_EXPECT_TRUE( fixture->guestLogin( 5, 0, grant, oldClient ) == LoginResult::UpdateRequired );
    SW_EXPECT_EQUAL( string( "https://store.example/game" ), grant._storeUrl );
    SW_EXPECT_EQUAL( 0, fixture._database.countRecords( hashed_string( "account_guest" ) ) ); // 일을 맡기지 않았다

    AccountClientInfo behind = oldClient;
    behind._build            = "1.9.9";
    SW_ASSERT_TRUE( fixture->guestLogin( 5, 10, grant, behind ) == LoginResult::Ok );
    SW_EXPECT_TRUE( grant._bUpdateRecommended == SW_TRUE );
    SW_EXPECT_EQUAL( string( "https://store.example/game" ), grant._storeUrl );

    AccountClientInfo current = oldClient;
    current._build            = "1.10.0";
    SW_ASSERT_TRUE( fixture->guestLogin( 5, 20, grant, current ) == LoginResult::Ok );
    SW_EXPECT_TRUE( grant._bUpdateRecommended == SW_FALSE );
    SW_EXPECT_TRUE( grant._storeUrl.empty() );
}

SW_TEST_CASE( LoginServiceTest, SuspendedAccountCannotLoginAndIsKicked )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture fixture;
    uint64       accountId = 0;
    SW_ASSERT_TRUE( fixture->registerAccount( Internal::makeCredential( "rule_breaker", "password123" ), 0, &accountId ) == LoginResult::Ok );
    LoginGrant grant;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "rule_breaker", "password123" ), 1, 0, grant ) == LoginResult::Ok );
    vector<LoginEvent> listEvent;
    fixture->drainEvents( listEvent );

    TestLoginServiceGuestInternal::writeSanction( fixture._database, accountId, ServiceSanctionKind::Suspend, 5000 );
    SW_ASSERT_TRUE( fixture->revokeAccountSessions( accountId, LoginRevokeReason::Sanctioned, 100 ) == LoginResult::Ok );
    listEvent.clear();
    fixture->drainEvents( listEvent );
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == LoginEvent::Kind::Revoked );
    SW_EXPECT_TRUE( listEvent[0]._reason == LoginRevokeReason::Sanctioned );
    SW_EXPECT_FALSE( fixture->isAccountOnline( accountId ) );

    LoginGrant refused;
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "rule_breaker", "password123" ), 2, 200, refused ) == LoginResult::AccountSuspended );
    SW_EXPECT_EQUAL( int64( 5000 ), refused._sanctionUntilMs );
    SW_EXPECT_EQUAL( string( "sanction.cheat" ), refused._sanctionReasonCode );
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "rule_breaker", "wrong-pass" ), 3, 200, refused ) == LoginResult::WrongCredentials ); // 비밀번호가 먼저
    LoginGrant after;
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "rule_breaker", "password123" ), 4, 5000, after ) == LoginResult::Ok ); // 끝나면 다시

    TestLoginServiceGuestInternal::writeSanction( fixture._database, accountId, ServiceSanctionKind::Ban, ServiceSanctionState::kPermanentMs );
    LoginGrant resumed;
    SW_EXPECT_TRUE( fixture->resumeSession( after._token, 4, 6000, resumed ) == LoginResult::AccountSuspended ); // 재접속도 막는다
}

SW_TEST_CASE( LoginServiceTest, DeletionWaitsForTheGraceThenErasesTheAccountButKeepsAudit )
{
    using Internal = TestLoginServiceInternal;
    LoginFixture              fixture;
    FakePlatformLoginProvider provider{ "fake" };
    SW_ASSERT_TRUE( fixture->_service->registerPlatformProvider( &provider ) );
    LoginGrant guest;
    SW_ASSERT_TRUE( fixture->guestLogin( 6, 0, guest ) == LoginResult::Ok );
    AccountIdentity identity;
    SW_ASSERT_TRUE( fixture->linkCredential( guest._token, Internal::makeCredential( "leaver", "password123" ), 10, identity ) == LoginResult::Ok );
    SW_ASSERT_TRUE( fixture->linkPlatform( guest._token, "fake", "subject:leave-1", 20 ) == LoginResult::Ok );
    const uint64 accountId = guest._identity._accountId;

    int64 dueMs = 0;
    SW_ASSERT_TRUE( fixture->requestDeletion( guest._token, 100, dueMs ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( 100 + fixture._settings._deletionGraceMs, dueMs );
    SW_EXPECT_FALSE( fixture->isAccountOnline( accountId ) ); // 요청이 세션을 끊는다
    AccountIdentity ignored;
    SW_EXPECT_TRUE( fixture->validateSession( guest._token, 110, ignored ) == LoginResult::Revoked );

    // 유예 안 — 로그인은 되고(예약 시각을 알려 준다) 취소할 수 있다.
    LoginGrant during;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "leaver", "password123" ), 1, 200, during ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( dueMs, during._deletionDueMs );
    fixture->purgeDueDeletions( dueMs - 1 ); // 아직
    SW_ASSERT_TRUE( fixture->cancelDeletion( during._token, 300 ) == LoginResult::Ok );
    fixture->purgeDueDeletions( dueMs + 1 ); // 취소된 예약 — 지우지 않는다
    LoginGrant kept;
    SW_ASSERT_TRUE( fixture->login( Internal::makeCredential( "leaver", "password123" ), 1, dueMs + 2, kept ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), kept._deletionDueMs );

    // 다시 요청하고 유예가 지나면 지운다 — 이름 · 장치 · 외부 연결이 모두 풀린다. 감사 줄은 남는다.
    int64 secondDueMs = 0;
    SW_ASSERT_TRUE( fixture->requestDeletion( kept._token, dueMs + 10, secondDueMs ) == LoginResult::Ok );
    const int32 auditBefore = fixture._database.countRecords( ServiceAuditLog::getTable() );
    fixture->purgeDueDeletions( secondDueMs );
    SW_EXPECT_EQUAL( auditBefore + 1, fixture._database.countRecords( ServiceAuditLog::getTable() ) );
    LoginGrant gone;
    SW_EXPECT_TRUE( fixture->login( Internal::makeCredential( "leaver", "password123" ), 1, secondDueMs + 1, gone ) == LoginResult::WrongCredentials );
    SW_ASSERT_TRUE( fixture->platformLogin( "fake", "subject:leave-1", secondDueMs + 1, gone ) == LoginResult::Ok );
    SW_EXPECT_TRUE( gone._bCreated == SW_TRUE ); // 같은 외부 주체는 새 계정
    SW_EXPECT_NOT_EQUAL( accountId, gone._identity._accountId );
    SW_ASSERT_TRUE( fixture->guestLogin( 6, secondDueMs + 1, gone ) == LoginResult::Ok );
    SW_EXPECT_TRUE( gone._bCreated == SW_TRUE );
    SW_EXPECT_EQUAL( 0, fixture._database.countRecords( hashed_string( "account_deletion" ) ) );
}
