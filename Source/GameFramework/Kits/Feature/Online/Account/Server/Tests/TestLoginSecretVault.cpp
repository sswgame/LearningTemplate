// LoginSecretVaultTest — 연동 비밀 봉인: 형식 [버전][nonce][암호문 + 태그], 같은 키 · 같은 문맥(제공자 이름)에서만 풀리고, 바꾼 바이트 · 짧은 바이트 · 다른 버전 · 준비 전은 거절한다.
// 암호는 가짜 AEAD(`FakeNetSecurityProvider`, 키 · nonce · AAD 를 섞은 태그)로 본다 — 실제 AES-GCM 은 OpenSSL 제공자의 몫이다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginSecretVault.h"
#include "GameFramework/Runtime/Online/Security/Tests/FakeNetSecurityProvider.h"

using namespace sw;

namespace
{
    struct LoginSecretVaultTestUtil
    {
        static void fillKey( uint8 ( &arrKey )[LoginSecretVault::kKeySize], uint8 seed )
        {
            for ( int32 index = 0; index < LoginSecretVault::kKeySize; ++index )
            {
                arrKey[index] = static_cast<uint8>( seed + index );
            }
        }
    };

    /** @brief 난수와 AEAD 를 켠 가짜 제공자에 키를 박은 봉인 도구입니다. */
    struct VaultFixture
    {
        test::FakeNetSecurityProvider _security{};
        LoginSecretVault              _vault{};

        explicit VaultFixture( uint8 keySeed = 7 )
        {
            _security._bRandomOk = SW_TRUE;
            _security._bAeadOk   = SW_TRUE;
            uint8 arrKey[LoginSecretVault::kKeySize];
            LoginSecretVaultTestUtil::fillKey( arrKey, keySeed );
            _vault.initialize( &_security, arrKey );
        }
    };
} // namespace

/** @brief [LoginSecretVaultTest] 봉인한 비밀은 같은 문맥으로 풀리고, 형식은 [버전 1][nonce 12][암호문 + 태그 16] 이며 봉인마다 nonce 가 다르다 */
SW_TEST_CASE( LoginSecretVaultTest, SealThenOpenRoundTrips )
{
    VaultFixture  rig;
    vector<uint8> listSealed;
    SW_REQUIRE_TRUE( rig._vault.seal( "refresh-token-abc", "apple", listSealed ) );
    SW_EXPECT_EQUAL( listSealed.size(), size_t( 1 + NetSecurityConstant::kAeadNonceSize + 17 + NetSecurityConstant::kAeadTagSize ) );
    SW_EXPECT_EQUAL( listSealed[0], LoginSecretVault::kFormatVersion );

    string plain;
    SW_REQUIRE_TRUE( rig._vault.open( listSealed, "apple", plain ) );
    SW_EXPECT_EQUAL( string( "refresh-token-abc" ), plain );

    vector<uint8> listSecond;
    SW_REQUIRE_TRUE( rig._vault.seal( "refresh-token-abc", "apple", listSecond ) );
    SW_EXPECT_TRUE( listSecond != listSealed ); // 같은 평문이라도 nonce 가 달라 봉인이 다르다
}

/** @brief [LoginSecretVaultTest] 다른 제공자 이름(문맥)이나 다른 키로는 풀리지 않는다 — 다른 제공자의 레코드를 바꿔 끼울 수 없다 */
SW_TEST_CASE( LoginSecretVaultTest, OtherContextOrKeyDoesNotOpen )
{
    VaultFixture  rig;
    vector<uint8> listSealed;
    SW_REQUIRE_TRUE( rig._vault.seal( "secret", "apple", listSealed ) );

    string plain;
    SW_EXPECT_FALSE( rig._vault.open( listSealed, "google", plain ) );
    VaultFixture otherKey( 99 );
    SW_EXPECT_FALSE( otherKey._vault.open( listSealed, "apple", plain ) );
    SW_EXPECT_TRUE( plain.empty() );
}

/** @brief [LoginSecretVaultTest] 바꾼 바이트, 태그보다 짧은 바이트, 다른 형식 버전은 거절한다 */
SW_TEST_CASE( LoginSecretVaultTest, TamperedShortOrOtherVersionIsRejected )
{
    VaultFixture  rig;
    vector<uint8> listSealed;
    SW_REQUIRE_TRUE( rig._vault.seal( "secret", "apple", listSealed ) );
    string plain;

    vector<uint8> listTampered = listSealed;
    listTampered[1 + NetSecurityConstant::kAeadNonceSize] ^= 0x01; // 암호문 첫 바이트
    SW_EXPECT_FALSE( rig._vault.open( listTampered, "apple", plain ) );

    vector<uint8> listOtherVersion = listSealed;
    listOtherVersion[0]            = static_cast<uint8>( LoginSecretVault::kFormatVersion + 1 );
    SW_EXPECT_FALSE( rig._vault.open( listOtherVersion, "apple", plain ) );

    const vector<uint8> listShort( static_cast<size_t>( NetSecurityConstant::kAeadNonceSize + NetSecurityConstant::kAeadTagSize ), LoginSecretVault::kFormatVersion );
    SW_EXPECT_FALSE( rig._vault.open( listShort, "apple", plain ) );
    SW_EXPECT_FALSE( rig._vault.open( vector<uint8>{}, "apple", plain ) );

    vector<uint8> listEmptyPlain; // 빈 평문도 봉인되고 태그만 남는다
    SW_REQUIRE_TRUE( rig._vault.seal( "", "apple", listEmptyPlain ) );
    SW_EXPECT_TRUE( rig._vault.open( listEmptyPlain, "apple", plain ) );
    SW_EXPECT_TRUE( plain.empty() );
}

/** @brief [LoginSecretVaultTest] 준비 전 · 종료 뒤 · 난수나 AEAD 를 만들지 못하면 봉인하지 않는다 — 평문으로 저장하는 길이 없다 */
SW_TEST_CASE( LoginSecretVaultTest, NotReadyOrMissingCryptoRefuses )
{
    LoginSecretVault idle;
    vector<uint8>    listSealed;
    string           plain;
    SW_EXPECT_FALSE( idle.isReady() );
    SW_EXPECT_FALSE( idle.seal( "secret", "apple", listSealed ) );
    SW_EXPECT_TRUE( listSealed.empty() );

    VaultFixture rig;
    SW_REQUIRE_TRUE( rig._vault.seal( "secret", "apple", listSealed ) );
    rig._security._bRandomOk = SW_FALSE;
    vector<uint8> listNoRandom;
    SW_EXPECT_FALSE( rig._vault.seal( "secret", "apple", listNoRandom ) );
    rig._security._bAeadOk = SW_FALSE;
    SW_EXPECT_FALSE( rig._vault.open( listSealed, "apple", plain ) );

    rig._security._bAeadOk = SW_TRUE;
    rig._vault.shutdown();
    SW_EXPECT_FALSE( rig._vault.isReady() );
    SW_EXPECT_FALSE( rig._vault.open( listSealed, "apple", plain ) );
}
