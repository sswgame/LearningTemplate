#include "pch.h"

#include "Core/Common/SecureUtil.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [SecureUtilTest] SipHash-2-4 가 참고 구현의 테스트 벡터와 같다(키 00..0f, 입력 00..0e · 빈 입력 · 8 바이트 경계)
 */
SW_TEST_CASE( SecureUtilTest, SipHashMatchesReferenceVectors )
{
    const sw::SipHashKey key{ 0x0706050403020100ull, 0x0F0E0D0C0B0A0908ull };
    uint8                arrInput[16]{};
    for ( uint8 index = 0; index < 16; ++index )
    {
        arrInput[index] = index;
    }
    SW_EXPECT_EQUAL( uint64{ 0x726FDB47DD0E0E31ull }, sw::SecureUtil::sipHash24( key, arrInput, 0 ) );
    SW_EXPECT_EQUAL( uint64{ 0x93F5F5799A932462ull }, sw::SecureUtil::sipHash24( key, arrInput, 8 ) );
    SW_EXPECT_EQUAL( uint64{ 0xA129CA6149BE45E5ull }, sw::SecureUtil::sipHash24( key, arrInput, 15 ) );
}

/**
 * @brief [SecureUtilTest] 상수 시간 비교 — 같은 바이트는 참, 한 바이트라도 다르거나 길이가 다르면 거짓
 */
SW_TEST_CASE( SecureUtilTest, ConstantTimeEqualityMatchesPlainEquality )
{
    const uint8 arrLeft[4]  = { 1, 2, 3, 4 };
    const uint8 arrRight[4] = { 1, 2, 3, 5 };
    SW_EXPECT_TRUE( sw::SecureUtil::equalsConstantTime( arrLeft, arrLeft, 4 ) );
    SW_EXPECT_FALSE( sw::SecureUtil::equalsConstantTime( arrLeft, arrRight, 4 ) );
    SW_EXPECT_TRUE( sw::SecureUtil::equalsConstantTime( arrLeft, arrRight, 3 ) );
    SW_EXPECT_TRUE( sw::SecureUtil::equalsConstantTime( std::string_view( "token" ), std::string_view( "token" ) ) );
    SW_EXPECT_FALSE( sw::SecureUtil::equalsConstantTime( std::string_view( "token" ), std::string_view( "tokem" ) ) );
    SW_EXPECT_FALSE( sw::SecureUtil::equalsConstantTime( std::string_view( "token" ), std::string_view( "token2" ) ) );
}
