// NetSecurityLoginCryptoTest — 로그인 암호의 실제 구현이 제공자(OpenSSL)를 맞게 부르는지 본다: HKDF 는 RFC 5869 시험 값, Argon2id 는 같은 입력 같은 출력 · 솔트가 다르면 다른 출력.
#include "pch.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

using namespace sw;

namespace
{
    struct NetSecurityLoginCryptoTestUtil
    {
        /** @brief 테스트가 빨리 끝나게 작은 Argon2id 값을 씁니다(운영 값은 `NetPasswordHashParams` 기본). */
        static NetPasswordHashParams makeSmallParams()
        {
            NetPasswordHashParams params;
            params._memoryKiB      = 64;
            params._iterationCount = 1;
            params._parallelism    = 1;
            return params;
        }
    };
} // namespace

/** @brief [NetSecurityLoginCryptoTest] 키 섞기는 솔트 없는 HKDF-SHA256 이다 — RFC 5869 시험 3(솔트 · 정보 없음)과 같은 바이트가 나온다. */
SW_TEST_CASE( NetSecurityLoginCryptoTest, KeyedHashMatchesRfc5869 )
{
    NetSecurityLoginCrypto crypto{ &game::getSubsystem<INetSecurityProvider>() };
    uint8                  arrKey[22];
    for ( uint8& value : arrKey )
    {
        value = 0x0b;
    }
    static constexpr uint8 kArrExpected[42] = { 0x8d, 0xa4, 0xe7, 0x75, 0xa5, 0x63, 0xc1, 0x8f, 0x71, 0x5f, 0x80, 0x2a, 0x06, 0x3c, 0x5a, 0x31, 0xb8, 0xa1, 0x1f, 0x5c, 0x5e,
                                                0xe1, 0x87, 0x9e, 0xc3, 0x45, 0x4e, 0x5f, 0x3c, 0x73, 0x8d, 0x2d, 0x9d, 0x20, 0x13, 0x95, 0xfa, 0xa4, 0xb6, 0x1a, 0x96, 0xc8 };
    uint8                  arrOut[42]       = {};
    SW_REQUIRE_TRUE( crypto.computeKeyedHash( arrKey, 22, nullptr, 0, arrOut, 42 ) );
    for ( int32 index = 0; index < 42; ++index )
    {
        SW_EXPECT_EQUAL( static_cast<int32>( kArrExpected[index] ), static_cast<int32>( arrOut[index] ) );
    }
}

/** @brief [NetSecurityLoginCryptoTest] Argon2id 는 같은 암호 · 솔트면 같은 해시, 솔트가 다르면 다른 해시다. 기동 확인(`isPasswordHashSupported`)도 통과한다. */
SW_TEST_CASE( NetSecurityLoginCryptoTest, PasswordHashIsDeterministicPerSalt )
{
    NetSecurityLoginCrypto      crypto{ &game::getSubsystem<INetSecurityProvider>() };
    const NetPasswordHashParams params = NetSecurityLoginCryptoTestUtil::makeSmallParams();
    SW_REQUIRE_TRUE( crypto.isPasswordHashSupported( params ) );

    uint8 arrSaltA[16]  = {};
    uint8 arrSaltB[16]  = {};
    arrSaltB[0]         = 1;
    uint8 arrFirst[32]  = {};
    uint8 arrSecond[32] = {};
    uint8 arrOther[32]  = {};
    SW_REQUIRE_TRUE( crypto.computePasswordHash( "correct horse", arrSaltA, 16, params, arrFirst, 32 ) );
    SW_REQUIRE_TRUE( crypto.computePasswordHash( "correct horse", arrSaltA, 16, params, arrSecond, 32 ) );
    SW_REQUIRE_TRUE( crypto.computePasswordHash( "correct horse", arrSaltB, 16, params, arrOther, 32 ) );
    bool bSame      = true;
    bool bDifferent = false;
    for ( int32 index = 0; index < 32; ++index )
    {
        bSame      = bSame && arrFirst[index] == arrSecond[index];
        bDifferent = bDifferent || arrFirst[index] != arrOther[index];
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( bDifferent );
}

/** @brief [NetSecurityLoginCryptoTest] 난수는 채우고, 제공자가 없으면 모든 연산이 실패로 답한다(빈 바이트를 성공으로 넘기지 않는다). */
SW_TEST_CASE( NetSecurityLoginCryptoTest, RandomFillsAndMissingProviderFails )
{
    NetSecurityLoginCrypto crypto{ &game::getSubsystem<INetSecurityProvider>() };
    uint8                  arrFirst[32]  = {};
    uint8                  arrSecond[32] = {};
    SW_REQUIRE_TRUE( crypto.fillRandom( arrFirst, 32 ) );
    SW_REQUIRE_TRUE( crypto.fillRandom( arrSecond, 32 ) );
    bool bDifferent = false;
    for ( int32 index = 0; index < 32; ++index )
    {
        bDifferent = bDifferent || arrFirst[index] != arrSecond[index];
    }
    SW_EXPECT_TRUE( bDifferent );

    NetSecurityLoginCrypto missing{ nullptr };
    uint8                  arrOut[16] = {};
    SW_EXPECT_FALSE( missing.fillRandom( arrOut, 16 ) );
    SW_EXPECT_FALSE( missing.computeKeyedHash( arrOut, 16, nullptr, 0, arrOut, 16 ) );
    SW_EXPECT_FALSE( missing.computePasswordHash( "x", arrOut, 16, NetSecurityLoginCryptoTestUtil::makeSmallParams(), arrOut, 16 ) );
}
