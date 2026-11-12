// LoginStoreLogicTest — 로그인 저장 규칙(메모리 저장소, 결정적인 가짜 암호): 가입 · 로그인 · 틀린 비밀번호(없는 계정과 구분하지 않는다) · 잠금(실패 상한) ·
// 이름 겹침(대소문자 무시) · 이름 형식 · 세션 이어 받기 · 로그아웃 뒤 토큰 · 게스트에 이름 붙이기 · 마지막 로그인 수단 · 탈퇴 유예와 취소. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr int64 kNowMs = 1700000000000ll;

    /** @brief 결정적인 가짜 암호입니다 — 같은 입력이면 같은 해시, 난수는 세는 값. 규칙(저장 · 비교)만 본다. */
    class FakeLoginCrypto final : public ILoginCrypto
    {
    public:
        bool fillRandom( uint8* pOut, int32 size ) override
        {
            for ( int32 index = 0; index < size; ++index )
                pOut[index] = static_cast<uint8>( ++_counter );
            return true;
        }
        bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& /*params*/, uint8* pOut, int32 outSize ) override
        {
            return fill( reinterpret_cast<const uint8*>( password.data() ), static_cast<int32>( password.size() ), pSalt, saltSize, pOut, outSize );
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
                    hash = HashUtil::fnvStep64( hash, pA[a] );
                for ( int32 b = 0; b < bSize; ++b )
                    hash = HashUtil::fnvStep64( hash, pB[b] );
                hash        = HashUtil::fnvStep64( hash, static_cast<uint8>( index ) );
                pOut[index] = static_cast<uint8>( hash >> 24 );
            }
            return true;
        }

        uint32 _counter{ 0 };
    };

    struct Fixture
    {
        MemoryServiceDatabase database;
        FakeLoginCrypto       crypto;
        LoginServiceConfig    settings;
        LoginTicketAuthority  ticketAuthority;

        Fixture()
        {
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize]{ 1, 2, 3 };
            ticketAuthority.initialize( &crypto, arrMasterKey );
        }

        LoginServiceResult registerAccount( string_view name, string_view password )
        {
            LoginStoreResult result;
            LoginStoreLogic  logic( database, crypto, settings, ticketAuthority, result );
            return logic.registerAccount( LoginCredential{ string( name ), string( password ) }, nullptr );
        }

        LoginServiceResult login( string_view name, string_view password, LoginGrant& outGrant, int64 nowMs = kNowMs )
        {
            LoginStoreResult result;
            LoginStoreLogic  logic( database, crypto, settings, ticketAuthority, result );
            return logic.login( LoginCredential{ string( name ), string( password ) }, nowMs, outGrant );
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
        SW_EXPECT_TRUE( logic.linkCredential( guestGrant._token, LoginCredential{ "frank_07", "other horse" }, kNowMs + 2, identity ) == LoginResult::AlreadyLinked );
        SW_REQUIRE_TRUE( logic.linkCredential( guestGrant._token, LoginCredential{ "Grace_08", "correct horse" }, kNowMs + 3, identity ) == LoginResult::Ok );
        SW_EXPECT_TRUE( logic.linkCredential( guestGrant._token, LoginCredential{ "Grace_09", "correct horse" }, kNowMs + 4, identity ) == LoginResult::AlreadyLinked );
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
    SW_REQUIRE_TRUE( logic.linkCredential( grant._token, LoginCredential{ "Heidi_10", "correct horse" }, kNowMs + 3, identity ) == LoginResult::Ok );
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
    SW_REQUIRE_TRUE( logic.linkCredential( grant._token, LoginCredential{ "Heidi_11", "correct horse" }, kNowMs + 1, identity ) == LoginResult::Ok );
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

    LoginGrant ivanGrant;
    SW_REQUIRE_TRUE( fixture.login( "Ivan_11", "correct horse", ivanGrant, kNowMs + 1000 ) == LoginResult::Ok ); // 유예 안이면 들어온다
    SW_EXPECT_EQUAL( dueMs, ivanGrant._deletionDueMs );
    {
        LoginStoreResult result;
        LoginStoreLogic  logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, result );
        SW_REQUIRE_TRUE( logic.cancelDeletion( ivanGrant._token, kNowMs + 2000 ) == LoginResult::Ok );
        SW_EXPECT_EQUAL( 1, logic.purgeDueDeletions( dueMs + 1, 8 ) ); // Judy 만
    }
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
