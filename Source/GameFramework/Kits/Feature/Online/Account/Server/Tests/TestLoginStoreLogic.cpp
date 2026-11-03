// LoginStoreLogicTest — 로그인 저장 규칙(메모리 저장소, 결정적인 가짜 암호): 가입 · 로그인 · 틀린 비밀번호(없는 계정과 구분하지 않는다) · 잠금(실패 상한) ·
// 이름 겹침(대소문자 무시) · 이름 형식 · 세션 이어 받기 · 로그아웃 뒤 토큰. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"
#include "GameFramework/Runtime/Online/Store/MemoryServiceStore.h"

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

        LoginResult registerAccount( string_view name, string_view password )
        {
            LoginStoreOutcome outcome;
            LoginStoreLogic   logic( database, crypto, settings, ticketAuthority, outcome );
            return logic.registerAccount( LoginCredential{ string( name ), string( password ) }, nullptr );
        }

        LoginResult login( string_view name, string_view password, LoginGrant& outGrant, int64 nowMs = kNowMs )
        {
            LoginStoreOutcome outcome;
            LoginStoreLogic   logic( database, crypto, settings, ticketAuthority, outcome );
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
        LoginStoreOutcome outcome;
        LoginStoreLogic   logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, outcome );
        AccountIdentity   identity;
        SW_EXPECT_TRUE( logic.validateSession( grant._token, kNowMs + 1000, identity ) == LoginResult::Ok );
        SW_EXPECT_TRUE( logic.logout( grant._token, kNowMs + 2000 ) == LoginResult::Ok );
    }
    LoginStoreOutcome outcome;
    LoginStoreLogic   logic( fixture.database, fixture.crypto, fixture.settings, fixture.ticketAuthority, outcome );
    AccountIdentity   identity;
    SW_EXPECT_TRUE( logic.validateSession( grant._token, kNowMs + 3000, identity ) != LoginResult::Ok );
}
