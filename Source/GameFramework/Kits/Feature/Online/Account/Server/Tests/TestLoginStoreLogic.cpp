// LoginStoreLogicTest — 로그인 저장 규칙(메모리 저장소, 결정적인 가짜 암호): 가입 · 로그인 · 틀린 비밀번호(없는 계정과 구분하지 않는다) · 잠금(실패 상한) ·
// 이름 겹침(대소문자 무시) · 이름 형식 · 세션 이어 받기 · 로그아웃 뒤 토큰 · 게스트에 이름 붙이기 · 마지막 로그인 수단 · 탈퇴 유예와 취소. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/Tests/ServiceStoreEntryTestUtil.h"

using namespace sw;

namespace
{
    constexpr int64 kNowMs = 1700000000000ll;

    /** @brief 탈퇴 뒤 키트 훅의 가짜입니다. 처음 @p _conflictCount 번은 Conflict(덜 지움)를 내고 그 뒤 Ok 이며, 받은 계정을 적습니다. */
    class FakeAfterDeleteAccount final : public IAfterDeleteAccount
    {
    public:
        mutable vector<AccountID> _listAccountID{};
        mutable int32             _conflictCount{ 1 };

        ServiceStoreResult afterDeleteAccount( IServiceStoreConnection& /*connection*/, AccountID accountID, int64 /*nowMs*/ ) const override
        {
            _listAccountID.push_back( accountID );
            if ( _conflictCount > 0 )
            {
                --_conflictCount;
                return ServiceStoreResult::Conflict;
            }
            return ServiceStoreResult::Ok;
        }
    };

    /** @brief 결정적인 가짜 암호입니다 — 같은 입력이면 같은 해시, 난수는 세는 값. 규칙(저장 · 비교)만 본다. */
    class FakeLoginCrypto final : public ILoginCrypto
    {
    public:
        uint32 _counter{ 0 };

        bool fillRandom( uint8* pOut, int32 size ) override
        {
            for ( int32 index = 0; index < size; ++index )
                pOut[index] = static_cast<uint8>( ++_counter );
            return true;
        }
        bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) override
        {
            return fill( pKey, keySize, pInfo, infoSize, pOut, outSize );
        }

        static bool fill( const uint8* pA, int32 aSize, const uint8* pB, int32 bSize, uint8* pOut, int32 outSize )
        {
            uint64 hash = HashUtil::kFNVOffset64;
            for ( int32 index = 0; index < outSize; ++index )
            {
                for ( int32 a = 0; a < aSize; ++a )
                    hash = HashUtil::fnvStep64( hash, pA[a] );
                for ( int32 b = 0; b < bSize; ++b )
                    hash = HashUtil::fnvStep64( hash, pB[b] );
                hash        = HashUtil::fnvStep64( hash, static_cast<uint8>( index ) );
                pOut[index] = static_cast<uint8>( hash >> 24 );
            }
            return true;
        }
    };

    /** @brief 결정적인 가짜 비밀번호 해시입니다. 부른 수를 셉니다 — 저장 규칙은 해시를 하지 않고 해시를 마친 값만 받는다(감사 O-3). */
    class FakeLoginHasher final : public IPasswordHasher
    {
    public:
        int32 _callCount{ 0 };

        bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& /*params*/, uint8* pOut, int32 outSize ) override
        {
            ++_callCount;
            return FakeLoginCrypto::fill( reinterpret_cast<const uint8*>( password.data() ), static_cast<int32>( password.size() ), pSalt, saltSize, pOut, outSize );
        }
    };

    struct Fixture
    {
        MemoryServiceDatabase database;
        FakeLoginCrypto       crypto;
        FakeLoginHasher       hasher;
        LoginServiceConfig    settings;
        LoginTicketAuthority  ticketAuthority;

        Fixture()
        {
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize]{ 1, 2, 3 };
            ticketAuthority.initialize( &crypto, arrMasterKey );
        }

        /** @brief 해시 줄이 하는 일(새 솔트 · 설정 매개변수로 해시)을 흉내 냅니다. */
        PasswordHashRecord prepare( string_view password )
        {
            PasswordHashRecord prepared;
            prepared._params = settings._passwordHashParams;
            SW_EXPECT_TRUE( crypto.fillRandom( prepared._arrSalt, LoginConstant::kSaltSize ) );
            SW_EXPECT_TRUE( hasher.computePasswordHash( password, prepared._arrSalt, LoginConstant::kSaltSize, prepared._params, prepared._arrHash, LoginConstant::kPasswordHashSize ) );
            return prepared;
        }

        LoginServiceResult registerAccount( string_view name, string_view password )
        {
            const LoginCredential    credential{ string( name ), string( password ) };
            const LoginServiceResult valid = LoginStoreLogic::validateNewCredential( credential ); // 서비스가 해시를 맡기기 전에 본다
            if ( valid != LoginResult::Ok )
                return valid;
            LoginStoreResult result;
            LoginStoreLogic  logic( database, crypto, settings, ticketAuthority, result );
            return logic.registerAccount( credential, prepare( password ), nullptr );
        }

        /** @brief 읽기 단계의 문제를 해시합니다(해시 줄의 몫). */
        PasswordHashResult hashChallenge( const PasswordHashChallenge& challenge, string_view password )
        {
            PasswordHashResult hashResult;
            SW_EXPECT_TRUE( hasher.computePasswordHash( password, challenge._arrSalt, LoginConstant::kSaltSize, challenge._params, hashResult._arrHash, LoginConstant::kPasswordHashSize ) );
            if ( challenge._bRehash == SW_TRUE )
            {
                hashResult._rehash     = prepare( password );
                hashResult._bHasRehash = SW_TRUE;
            }
            return hashResult;
        }

        /** @brief 비밀번호 로그인의 세 단계(읽기 → 해시 → 커밋)를 차례로 합니다. */
        LoginServiceResult login( string_view name, string_view password, LoginGrant& outGrant, int64 nowMs = kNowMs )
        {
            LoginStoreResult         result;
            LoginStoreLogic          logic( database, crypto, settings, ticketAuthority, result );
            PasswordHashChallenge    challenge;
            const LoginServiceResult read = logic.readLoginChallenge( name, nowMs, challenge, outGrant );
            if ( read != LoginResult::Ok )
                return read;
            const PasswordHashResult hashResult = hashChallenge( challenge, password );
            bool                     bHashStale = false;
            return logic.commitLogin( challenge, hashResult, nowMs, outGrant, bHashStale );
        }
    };
} // namespace

/** @brief [LoginStoreLogicTest] 가입한 이름 · 비밀번호로 들어오고, 이름은 대소문자를 가리지 않고 겹칠 수 없으며 형식 밖 이름은 받지 않는다 */
SW_TEST_CASE( LoginStoreLogicTest, RegisterThenLogin )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Alice_01", "correct horse" ) == LoginResult::Ok );
    SW_EXPECT_TRUE( fixture.registerAccount( "alice_01", "another pass" ) == LoginResult::NameTaken );
    SW_EXPECT_TRUE( fixture.registerAccount( "x", "correct horse" ) == LoginResult::InvalidName );
    SW_EXPECT_TRUE( fixture.registerAccount( "Bob_02", "short" ) == LoginResult::InvalidPassword );

    LoginGrant grant;
    SW_REQUIRE_TRUE( fixture.login( "ALICE_01", "correct horse", grant ) == LoginResult::Ok );
    SW_EXPECT_TRUE( grant._token.isEmpty() == false );
    SW_EXPECT_TRUE( grant._expiresAtMs > kNowMs );
}

/** @brief [LoginStoreLogicTest] 틀린 비밀번호와 없는 계정은 같은 결과(WrongCredentials)다 — 어느 쪽인지 알리지 않는다 */
SW_TEST_CASE( LoginStoreLogicTest, WrongPasswordLooksLikeUnknownAccount )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Carol_03", "correct horse" ) == LoginResult::Ok );
    LoginGrant grant;
    SW_EXPECT_TRUE( fixture.login( "Carol_03", "wrong horse!", grant ) == LoginResult::WrongCredentials );
    SW_EXPECT_TRUE( fixture.login( "Nobody_04", "correct horse", grant ) == LoginResult::WrongCredentials );
    SW_EXPECT_TRUE( grant._token.isEmpty() );
}

/** @brief [LoginStoreLogicTest] 실패가 상한에 닿으면 맞는 비밀번호도 잠금 동안 AccountLocked 이고, 잠금이 지나면 들어온다 */
SW_TEST_CASE( LoginStoreLogicTest, FailuresLockTheAccount )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Dave_05", "correct horse" ) == LoginResult::Ok );
    LoginGrant grant;
    for ( int32 attempt = 0; attempt < fixture.settings._maxFailedCount; ++attempt )
        (void)fixture.login( "Dave_05", "wrong horse!", grant );
    SW_EXPECT_TRUE( fixture.login( "Dave_05", "correct horse", grant ) == LoginResult::AccountLocked );
    SW_EXPECT_TRUE( grant._retryAfterMs > 0 );
    SW_EXPECT_TRUE( fixture.login( "Dave_05", "correct horse", grant, kNowMs + fixture.settings._lockoutMs + 1 ) == LoginResult::Ok );
}

/** @brief [LoginStoreLogicTest] 받은 토큰으로 세션을 이어 받고, 로그아웃한 토큰은 더 쓰지 못한다 */
SW_TEST_CASE( LoginStoreLogicTest, TokenResumesUntilLogout )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Erin_06", "correct horse" ) == LoginResult::Ok );
    LoginGrant grant;
    SW_REQUIRE_TRUE( fixture.login( "Erin_06", "correct horse", grant ) == LoginResult::Ok );
    {
        LoginStoreResult result;
        LoginStoreLogic  logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
        AccountIdentity  identity;
        SW_EXPECT_TRUE( logic.validateSession( grant._token, kNowMs + 1000, identity ) == LoginResult::Ok );
        SW_EXPECT_TRUE( logic.logout( grant._token, kNowMs + 2000 ) == LoginResult::Ok );
    }
    LoginStoreResult result;
    LoginStoreLogic  logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
    AccountIdentity  identity;
    SW_EXPECT_TRUE( logic.validateSession( grant._token, kNowMs + 3000, identity ) != LoginResult::Ok );
}

/** @brief [LoginStoreLogicTest] 게스트에 이름을 붙이면 그 이름으로 들어온다. 두 번째 이름 · 남의 이름은 AlreadyLinked 다 */
SW_TEST_CASE( LoginStoreLogicTest, GuestLinksCredentialOnce )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Frank_07", "correct horse" ) == LoginResult::Ok );
    const uint8 arrDeviceSecret[LoginConstant::kDeviceSecretSize]{ 7, 7, 7 };
    LoginGrant  guestGrant;
    {
        LoginStoreResult result;
        LoginStoreLogic  logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
        SW_REQUIRE_TRUE( logic.guestLogin( arrDeviceSecret, kNowMs, guestGrant ) == LoginResult::Ok );
        AccountLinkSummary summary;
        SW_REQUIRE_TRUE( logic.listLinks( guestGrant._token, kNowMs + 1, summary ) == LoginResult::Ok );
        SW_EXPECT_TRUE( summary._bGuest == SW_TRUE && summary._bHasCredential == SW_FALSE );

        AccountIdentity identity;
        SW_EXPECT_TRUE( logic.linkCredential( guestGrant._token, LoginCredential{ "frank_07", "other horse" }, fixture.prepare( "other horse" ), kNowMs + 2, identity ) == LoginResult::AlreadyLinked );
        SW_REQUIRE_TRUE( logic.linkCredential( guestGrant._token, LoginCredential{ "Grace_08", "correct horse" }, fixture.prepare( "correct horse" ), kNowMs + 3, identity ) == LoginResult::Ok );
        SW_EXPECT_TRUE( logic.linkCredential( guestGrant._token, LoginCredential{ "Grace_09", "correct horse" }, fixture.prepare( "correct horse" ), kNowMs + 4, identity ) == LoginResult::AlreadyLinked );
        SW_REQUIRE_TRUE( logic.listLinks( guestGrant._token, kNowMs + 5, summary ) == LoginResult::Ok );
        SW_EXPECT_TRUE( summary._bGuest == SW_FALSE && summary._bHasCredential == SW_TRUE );
    }
    LoginGrant grant;
    SW_EXPECT_TRUE( fixture.login( "Grace_08", "correct horse", grant, kNowMs + 10 ) == LoginResult::Ok );
}

/** @brief [LoginStoreLogicTest] 외부 계정만 있는 계정은 그것을 떼지 못하고(LastLoginMethod), 이름을 붙인 뒤에는 뗀다. 붙지 않은 제공자는 NotLinked 다 */
SW_TEST_CASE( LoginStoreLogicTest, LastLoginMethodStays )
{
    Fixture          fixture;
    LoginStoreResult result;
    LoginStoreLogic  logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
    LoginGrant       grant;
    SW_REQUIRE_TRUE( logic.platformLogin( "steam", "76561190000000001", "Heidi", nullptr, kNowMs, grant ) == LoginResult::Ok );
    SW_EXPECT_TRUE( grant._bCreated == SW_TRUE );
    SW_EXPECT_TRUE( logic.unlinkPlatform( grant._token, "steam", kNowMs + 1 ) == LoginResult::LastLoginMethod );
    SW_EXPECT_TRUE( logic.unlinkPlatform( grant._token, "epic", kNowMs + 2 ) == LoginResult::NotLinked );

    AccountIdentity identity;
    SW_REQUIRE_TRUE( logic.linkCredential( grant._token, LoginCredential{ "Heidi_10", "correct horse" }, fixture.prepare( "correct horse" ), kNowMs + 3, identity ) == LoginResult::Ok );
    SW_EXPECT_TRUE( logic.unlinkPlatform( grant._token, "steam", kNowMs + 4 ) == LoginResult::Ok );
    AccountLinkSummary summary;
    SW_REQUIRE_TRUE( logic.listLinks( grant._token, kNowMs + 5, summary ) == LoginResult::Ok );
    SW_EXPECT_TRUE( summary._listProvider.empty() );
}

/** @brief [LoginStoreLogicTest] 연동 조회: 그 계정 · 그 제공자 · 그 주체일 때만 true 다. 연동이 없거나 틀린 제공자 이름도 저장소 오류가 아니라 false 이고, 떼면 false 가 된다(영수증 구매자 대조가 쓴다) */
SW_TEST_CASE( LoginStoreLogicTest, ReadIsLinkedMatchesOnlyTheLinkedSubject )
{
    Fixture          fixture;
    LoginStoreResult result;
    LoginStoreLogic  logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
    LoginGrant       grant;
    SW_REQUIRE_TRUE( logic.platformLogin( "steam", "76561190000000001", "Heidi", nullptr, kNowMs, grant ) == LoginResult::Ok );
    const uint64 accountID = grant._identity._accountID;

    const auto isLinked = [&fixture]( uint64 account, string_view provider, string_view subject )
    {
        bool bLinked = false;
        SW_EXPECT_TRUE( LoginStoreLogic::readIsLinked( fixture.database, fixture.crypto, account, provider, subject, bLinked ) == ServiceStoreResult::Ok );
        return bLinked;
    };
    SW_EXPECT_TRUE( isLinked( accountID, "steam", "76561190000000001" ) );
    SW_EXPECT_FALSE( isLinked( accountID, "steam", "76561190000000002" ) );
    SW_EXPECT_FALSE( isLinked( accountID, "epic", "76561190000000001" ) );
    SW_EXPECT_FALSE( isLinked( accountID + 1, "steam", "76561190000000001" ) );
    SW_EXPECT_FALSE( isLinked( accountID, "Not A Token", "76561190000000001" ) );
    SW_EXPECT_FALSE( isLinked( accountID, "steam", "" ) );

    AccountIdentity identity;
    SW_REQUIRE_TRUE( logic.linkCredential( grant._token, LoginCredential{ "Heidi_11", "correct horse" }, fixture.prepare( "correct horse" ), kNowMs + 1, identity ) == LoginResult::Ok );
    SW_REQUIRE_TRUE( logic.unlinkPlatform( grant._token, "steam", kNowMs + 2 ) == LoginResult::Ok );
    SW_EXPECT_FALSE( isLinked( accountID, "steam", "76561190000000001" ) );
}

/** @brief [LoginStoreLogicTest] 탈퇴 예약은 세션을 끊고 유예 시각을 정한다. 유예 안에 다시 들어와 취소하면 지우지 않고, 취소하지 않으면 시각이 지난 뒤 지워 그 이름으로 들어오지 못한다 */
SW_TEST_CASE( LoginStoreLogicTest, DeletionWaitsForGraceAndCanBeCancelled )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Ivan_11", "correct horse" ) == LoginResult::Ok );
    SW_REQUIRE_TRUE( fixture.registerAccount( "Judy_12", "correct horse" ) == LoginResult::Ok );
    const int64    dueMs = kNowMs + fixture.settings._deletionGraceMs;
    vector<uint64> listAccountID;
    for ( const string_view name : { string_view( "Ivan_11" ), string_view( "Judy_12" ) } )
    {
        LoginGrant grant;
        SW_REQUIRE_TRUE( fixture.login( name, "correct horse", grant ) == LoginResult::Ok );
        listAccountID.push_back( grant._identity._accountID );
        LedgerTransferRequest coins;
        coins._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( grant._identity._accountID ), "cur.gold", 50 } );
        coins._journalKey = LedgerJournalKey::makeAccountScope( grant._identity._accountID );
        coins._journalKey += "/test.grant";
        coins._reason = "test.grant";
        coins._timeMs = kNowMs;
        LedgerTransferResult coinsResult;
        SW_REQUIRE_TRUE( Ledger::executeTransfer( fixture.database, coins, coinsResult ) == LedgerResult::Ok );
        LoginStoreResult result;
        LoginStoreLogic  logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
        int64            outDueMs = 0;
        SW_REQUIRE_TRUE( logic.requestDeletion( grant._token, kNowMs, outDueMs ) == LoginResult::Ok );
        SW_EXPECT_EQUAL( dueMs, outDueMs );
        AccountIdentity identity;
        SW_EXPECT_TRUE( logic.validateSession( grant._token, kNowMs + 1, identity ) != LoginResult::Ok ); // 예약이 세션을 끊었다
    }

    FakeAfterDeleteAccount                   hook;
    const vector<const IAfterDeleteAccount*> listHook{ &hook };
    LoginGrant                               ivanGrant;
    SW_REQUIRE_TRUE( fixture.login( "Ivan_11", "correct horse", ivanGrant, kNowMs + 1000 ) == LoginResult::Ok ); // 유예 안이면 들어온다
    SW_EXPECT_EQUAL( dueMs, ivanGrant._deletionDueMs );
    {
        LoginStoreResult result;
        LoginStoreLogic  logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
        SW_REQUIRE_TRUE( logic.cancelDeletion( ivanGrant._token, kNowMs + 2000 ) == LoginResult::Ok );
        SW_EXPECT_EQUAL( 1, logic.purgeDueDeletions( dueMs + 1, 8, listHook ) ); // Judy 만
        SW_EXPECT_EQUAL( 0, logic.purgeDueDeletions( dueMs + 2, 8, listHook ) ); // 훅이 처음엔 덜 지웠다(Conflict) — 대기열에 남아 다음 회차에 다시 부른다
        SW_EXPECT_EQUAL( 0, logic.purgeDueDeletions( dueMs + 3, 8, listHook ) ); // 다 지운 계정은 대기열에서 빠져 다시 부르지 않는다
    }
    SW_REQUIRE_EQUAL( size_t{ 2 }, hook._listAccountID.size() );
    SW_EXPECT_EQUAL( listAccountID[1], hook._listAccountID[0] );
    SW_EXPECT_EQUAL( listAccountID[1], hook._listAccountID[1] );
    LoginGrant grant;
    SW_EXPECT_TRUE( fixture.login( "Ivan_11", "correct horse", grant, dueMs + 2 ) == LoginResult::Ok );
    SW_EXPECT_EQUAL( int64{ 0 }, grant._deletionDueMs );
    SW_EXPECT_TRUE( fixture.login( "Judy_12", "correct horse", grant, dueMs + 3 ) == LoginResult::WrongCredentials );

    // 지운 Judy 의 잔액은 같은 정리에서 소각됐고 묘비가 남는다. 취소한 Ivan 의 잔액은 그대로다(감사 P0-7)
    LedgerBalance balance;
    SW_REQUIRE_TRUE( Ledger::readBalance( fixture.database, LedgerHolder::makeAccount( listAccountID[0] ), "cur.gold", balance ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( int64{ 50 }, balance._amount );
    SW_REQUIRE_TRUE( Ledger::readBalance( fixture.database, LedgerHolder::makeAccount( listAccountID[1] ), "cur.gold", balance ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( int64{ 0 }, balance._amount );
    ServiceRecord tombstone;
    SW_EXPECT_TRUE( fixture.database.readRecord( Ledger::getTombstoneTable(), LedgerHolder::makeAccount( listAccountID[1] ).makeKey(), tombstone ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( fixture.database.readRecord( Ledger::getTombstoneTable(), LedgerHolder::makeAccount( listAccountID[0] ).makeKey(), tombstone ) == ServiceStoreResult::NotFound );
}

/**
 * @brief [LoginStoreLogicTest] 계정 id 는 다시 쓰지 않는다: 지운 계정과 같은 id 가 뽑히면 묘비가 막아 새 id 로 만든다(지운 계정의 원장 묘비가 새 계정을 막지 않는다)
 * @details 가짜 암호의 난수는 세는 값이라 새 고정 장치 둘은 같은 id 를 뽑는다. 둘째 장치에 그 id 의 묘비를 먼저 세운다. 변이: `requireNoTombstone` 을 빼면 같은 id 로 만들어져 진다.
 */
SW_TEST_CASE( LoginStoreLogicTest, DeletedAccountIDIsNeverReused )
{
    uint64 firstID = 0;
    {
        Fixture          first;
        LoginStoreResult result;
        LoginStoreLogic  logic( first.database, first.crypto, first.settings, first.ticketAuthority, result );
        SW_REQUIRE_TRUE( logic.registerAccount( LoginCredential{ "Kate_13", "correct horse" }, first.prepare( "correct horse" ), &firstID ) == LoginResult::Ok );
    }
    Fixture            second;
    ServiceTransaction deletion;
    Ledger::stageAccountDeletion( firstID, kNowMs, deletion );
    SW_REQUIRE_TRUE( second.database.commit( deletion ) == ServiceStoreResult::Ok );
    uint64           secondID = 0;
    LoginStoreResult result;
    LoginStoreLogic  logic( second.database, second.crypto, second.settings, second.ticketAuthority, result );
    SW_REQUIRE_TRUE( logic.registerAccount( LoginCredential{ "Kate_13", "correct horse" }, second.prepare( "correct horse" ), &secondID ) == LoginResult::Ok );
    SW_EXPECT_TRUE( secondID != 0 );
    SW_EXPECT_TRUE( secondID != firstID );
}

/** @brief [LoginStoreLogicTest] 없는 이름 · 형식 밖 이름도 해시를 한 번 치르는 같은 길이다 — 응답 시간으로 계정 유무가 드러나지 않는다(감사 O-3) */
SW_TEST_CASE( LoginStoreLogicTest, UnknownNameStillPaysOneHash )
{
    Fixture     fixture;
    LoginGrant  grant;
    const int32 before = fixture.hasher._callCount;
    SW_EXPECT_TRUE( fixture.login( "Nobody_20", "correct horse", grant ) == LoginResult::WrongCredentials );
    SW_EXPECT_EQUAL( before + 1, fixture.hasher._callCount );
    SW_EXPECT_TRUE( fixture.login( "x", "correct horse", grant ) == LoginResult::WrongCredentials ); // 형식 밖 이름
    SW_EXPECT_EQUAL( before + 2, fixture.hasher._callCount );
}

/**
 * @brief [LoginStoreLogicTest] 읽기와 커밋 사이에 계정이 바뀌어도(다른 시도가 실패 수를 올렸다) 솔트 · 매개변수가 같으면 커밋이 계산한 해시를 그대로 쓴다 — 다시 해시하지 않는다
 */
SW_TEST_CASE( LoginStoreLogicTest, ConflictAfterHashReusesTheVerifiedHash )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Liam_21", "correct horse" ) == LoginResult::Ok );
    LoginStoreResult      result;
    LoginStoreLogic       logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
    LoginGrant            grant;
    PasswordHashChallenge challenge;
    SW_REQUIRE_TRUE( logic.readLoginChallenge( "Liam_21", kNowMs, challenge, grant ) == LoginResult::Ok );
    SW_REQUIRE_TRUE( challenge._bKnownAccount == SW_TRUE );
    const PasswordHashResult hashResult = fixture.hashChallenge( challenge, "correct horse" );

    LoginGrant other;
    SW_EXPECT_TRUE( fixture.login( "Liam_21", "wrong horse!", other ) == LoginResult::WrongCredentials ); // 그사이 계정 레코드의 버전이 바뀐다
    const int32 hashCount  = fixture.hasher._callCount;
    bool        bHashStale = true;
    SW_EXPECT_TRUE( logic.commitLogin( challenge, hashResult, kNowMs, grant, bHashStale ) == LoginResult::Ok );
    SW_EXPECT_FALSE( bHashStale );
    SW_EXPECT_EQUAL( hashCount, fixture.hasher._callCount ); // 커밋은 해시하지 않는다
    SW_EXPECT_TRUE( grant._token.isEmpty() == false );
}

/** @brief [LoginStoreLogicTest] 해시하는 사이 솔트가 바뀌면(매개변수가 바뀐 계정을 다른 로그인이 다시 해시했다) 커밋은 그 해시를 쓰지 않고 다시 해시하라고 답한다 */
SW_TEST_CASE( LoginStoreLogicTest, CommitAfterSaltChangeAsksForANewHash )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Mia_22", "correct horse" ) == LoginResult::Ok );
    fixture.settings._passwordHashParams._iterationCount += 1; // 설정이 바뀌었다 — 다음 로그인이 새 솔트로 다시 해시한다
    LoginStoreResult      result;
    LoginStoreLogic       logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
    LoginGrant            grant;
    PasswordHashChallenge challenge;
    SW_REQUIRE_TRUE( logic.readLoginChallenge( "Mia_22", kNowMs, challenge, grant ) == LoginResult::Ok );
    SW_EXPECT_TRUE( challenge._bRehash == SW_TRUE );
    const PasswordHashResult hashResult = fixture.hashChallenge( challenge, "correct horse" );

    LoginGrant other;
    SW_REQUIRE_TRUE( fixture.login( "Mia_22", "correct horse", other ) == LoginResult::Ok ); // 다른 로그인이 먼저 새 솔트로 바꿨다
    bool bHashStale = false;
    SW_EXPECT_TRUE( logic.commitLogin( challenge, hashResult, kNowMs, grant, bHashStale ) != LoginResult::Ok );
    SW_EXPECT_TRUE( bHashStale );
}

namespace
{
    /** @brief 첫 커밋 직전에 다른 워커의 틀린 로그인 하나를 끼워 넣는 연결입니다 — 둘이 같은 판의 계정 레코드를 읽고 겹쳐 쓴다. */
    class RacingLoginConnection final : public IServiceStoreConnection
    {
    public:
        explicit RacingLoginConnection( Fixture& fixture )
            : _fixture{ fixture }
        {
        }

        ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override
        {
            return _fixture.database.readRecord( table, key, outRecord );
        }

        ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                        vector<ServiceRecord>& outListRecord ) override
        {
            return _fixture.database.listRecords( table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord );
        }

        ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo ) override
        {
            if ( _bRaced == SW_FALSE )
            {
                _bRaced = SW_TRUE;
                LoginGrant grant;
                SW_EXPECT_TRUE( _fixture.login( "Erin_06", "wrong horse!", grant ) == LoginResult::WrongCredentials ); // 다른 워커가 먼저 센다
            }
            return _fixture.database.commit( transaction, pOutInfo );
        }

    private:
        Fixture& _fixture;
        uint8    _bRaced{ SW_FALSE };
    };
} // namespace

/** @brief [LoginStoreLogicTest] 동시에 겹친 틀린 비밀번호도 모두 센다 — 상한 2 에서 겹친 두 번이면 잠긴다(Conflict 를 버리면 한 번만 세어 잠금을 비켜 갔다, 감사 O-14) */
SW_TEST_CASE( LoginStoreLogicTest, RacingFailuresAreAllCounted )
{
    Fixture fixture;
    fixture.settings._maxFailedCount = 2;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Erin_06", "correct horse" ) == LoginResult::Ok );
    RacingLoginConnection racing{ fixture };
    LoginStoreResult      storeResult;
    LoginStoreLogic       logic( racing, fixture.crypto, fixture.settings, fixture.ticketAuthority, storeResult );
    LoginGrant            grant;
    PasswordHashChallenge challenge;
    SW_REQUIRE_TRUE( logic.readLoginChallenge( "Erin_06", kNowMs, challenge, grant ) == LoginResult::Ok );
    const PasswordHashResult hashResult = fixture.hashChallenge( challenge, "wrong horse!" );
    bool                     bHashStale = false;
    SW_EXPECT_TRUE( logic.commitLogin( challenge, hashResult, kNowMs, grant, bHashStale ) == LoginResult::AccountLocked ); // 커밋 단계의 실패 셈이 겹친 쓰기를 다시 읽어 센다
    SW_EXPECT_TRUE( fixture.login( "Erin_06", "correct horse", grant ) == LoginResult::AccountLocked );
}

/** @brief [LoginStoreLogicTest] 재접속이 충돌에 거듭 지면 kBusy 다 — InvalidToken 이면 클라이언트가 멀쩡한 세션을 죽은 것으로 보고 로그아웃한다. 충돌이 풀리면 같은 토큰으로 들어온다 */
SW_TEST_CASE( LoginStoreLogicTest, ResumeUnderConflictsIsBusyAndKeepsTheToken )
{
    SW_EXPECT_LOG( "ServiceStore.ConflictRetriesExhausted" );
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Frank_07", "correct horse" ) == LoginResult::Ok );
    LoginGrant grant;
    SW_REQUIRE_TRUE( fixture.login( "Frank_07", "correct horse", grant ) == LoginResult::Ok );
    const LoginSessionToken token = grant._token;

    UnreliableStoreConnection unreliable( fixture.database );
    LoginStoreResult          storeResult;
    LoginStoreLogic           logic( unreliable, fixture.crypto, fixture.settings, fixture.ticketAuthority, storeResult );
    unreliable._conflictCount = 1000;
    SW_EXPECT_TRUE( logic.resumeSession( token, kNowMs, grant ).hasError( OnlineError::kBusy ) );
    unreliable._conflictCount = 2; // 이만큼은 시도 안에서 풀린다
    SW_EXPECT_TRUE( logic.resumeSession( token, kNowMs, grant ) == LoginResult::Ok );
    SW_EXPECT_TRUE( logic.resumeSession( token, kNowMs, grant ) == LoginResult::InvalidToken ); // 비밀이 바뀌었다 — 이제야 죽은 토큰이다
}

/** @brief [LoginStoreLogicTest] 끊김 표시가 충돌에 거듭 지면 알리고, 몇 번의 충돌은 이겨 유예를 시작한다 — 예전에는 말없이 넘어가 유예가 시작되지 않았다 */
SW_TEST_CASE( LoginStoreLogicTest, DisconnectGraceSurvivesConflictsOrIsReported )
{
    SW_EXPECT_LOG( "ServiceStore.ConflictRetriesExhausted" );
    SW_EXPECT_LOG( "LoginStoreLogic.SessionCouldNotStart" );
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Grace_08", "correct horse" ) == LoginResult::Ok );
    LoginGrant grant;
    SW_REQUIRE_TRUE( fixture.login( "Grace_08", "correct horse", grant ) == LoginResult::Ok );

    UnreliableStoreConnection unreliable( fixture.database );
    LoginStoreResult          storeResult;
    LoginStoreLogic           logic( unreliable, fixture.crypto, fixture.settings, fixture.ticketAuthority, storeResult );
    unreliable._conflictCount = 1000;
    logic.markDisconnected( grant._token._sessionID, kNowMs );
    SW_EXPECT_EQUAL( size_t{ 1 }, storeResult._listDisconnected.size() ); // 거듭 시도해도 한 번만 알린다
    const int64     afterGraceMs = kNowMs + fixture.settings._reconnectGraceMs;
    AccountIdentity identity;
    unreliable._conflictCount = 0;
    SW_EXPECT_TRUE( logic.validateSession( grant._token, afterGraceMs, identity ) == LoginResult::Ok ); // 유예가 시작되지 않았다
    unreliable._conflictCount = 2;
    logic.markDisconnected( grant._token._sessionID, kNowMs );
    SW_EXPECT_TRUE( logic.validateSession( grant._token, afterGraceMs, identity ) == LoginResult::Expired );
}

/** @brief [LoginStoreLogicTest] 로그아웃이 충돌하면 다시 지운다 — 지우지 않고 Ok 로 답하면 로그아웃한 토큰이 살아 있다 */
SW_TEST_CASE( LoginStoreLogicTest, LogoutUnderConflictErasesTheSession )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( fixture.registerAccount( "Heidi_09", "correct horse" ) == LoginResult::Ok );
    LoginGrant grant;
    SW_REQUIRE_TRUE( fixture.login( "Heidi_09", "correct horse", grant ) == LoginResult::Ok );
    UnreliableStoreConnection unreliable( fixture.database );
    LoginStoreResult          storeResult;
    LoginStoreLogic           logic( unreliable, fixture.crypto, fixture.settings, fixture.ticketAuthority, storeResult );
    unreliable._conflictCount = 1;
    SW_EXPECT_TRUE( logic.logout( grant._token, kNowMs ) == LoginResult::Ok );
    AccountIdentity identity;
    SW_EXPECT_TRUE( logic.validateSession( grant._token, kNowMs, identity ) == LoginResult::InvalidToken );
}

/** @brief [LoginStoreLogicTest] 로그인이 충돌에 거듭 지면 kBusy 다 — 쓰지 않았으므로 멱등 키 없이도 클라이언트가 다시 보낸다(kUnavailable 이면 다시 보내지 않는다) */
SW_TEST_CASE( LoginStoreLogicTest, LoginUnderConflictsIsBusy )
{
    SW_EXPECT_LOG( "ServiceStore.ConflictRetriesExhausted" );
    Fixture                   fixture;
    UnreliableStoreConnection unreliable( fixture.database );
    unreliable._conflictCount = 1000;
    LoginStoreResult storeResult;
    LoginStoreLogic  logic( unreliable, fixture.crypto, fixture.settings, fixture.ticketAuthority, storeResult );
    const uint8      arrDeviceSecret[LoginConstant::kDeviceSecretSize]{ 9, 9, 9 };
    LoginGrant       grant;
    SW_EXPECT_TRUE( logic.guestLogin( arrDeviceSecret, kNowMs, grant ).hasError( OnlineError::kBusy ) );
    SW_EXPECT_TRUE( grant._token.isEmpty() );
}
