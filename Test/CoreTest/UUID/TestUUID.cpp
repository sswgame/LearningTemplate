#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/UUID/UUID.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) UUIDTest — 생성, 속성, 포맷팅 및 해시 검증
// ------------------------------------------------------------------------------

/**
 * @brief [UUIDTest] sw::UUID v4 생성 및 RFC 4122 규격 비트 검증
 */
SW_TEST_CASE( UUIDTest, GenerateAndProperties )
{
    const sw::UUID uuid1 = sw::UUID::generate();
    const sw::UUID uuid2 = sw::UUID::generate();

    SW_EXPECT_FALSE( uuid1.isNull() );
    SW_EXPECT_FALSE( uuid2.isNull() );
    SW_EXPECT_TRUE( uuid1 != uuid2 );

    // RFC 4122 v4 버전 비트 검증 (arrBytes[6] 상위 4비트는 0x4)
    const uint8 versionNibble = static_cast<uint8>( uuid1._arrBytes[6] >> 4 );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( versionNibble ) );

    // RFC 4122 variant 비트 검증 (arrBytes[8] 상위 2비트는 0b10 -> 0x8, 0x9, 0xA, 0xB)
    const uint8 variantBits = static_cast<uint8>( uuid1._arrBytes[8] >> 6 );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( variantBits ) );
}

/**
 * @brief [UUIDTest] Nil sw::UUID 및 비교 연산자 검증
 */
SW_TEST_CASE( UUIDTest, NullAndEquality )
{
    sw::UUID nilUUID{};
    SW_EXPECT_TRUE( nilUUID.isNull() );

    sw::UUID nilUUID2{};
    SW_EXPECT_TRUE( nilUUID == nilUUID2 );
    SW_EXPECT_FALSE( nilUUID != nilUUID2 );
    SW_EXPECT_FALSE( nilUUID < nilUUID2 );

    const sw::UUID generated = sw::UUID::generate();
    SW_EXPECT_FALSE( generated.isNull() );
    SW_EXPECT_TRUE( nilUUID != generated );
    SW_EXPECT_FALSE( nilUUID == generated );

    // 자기 자신과의 비교
    SW_EXPECT_TRUE( generated == generated );
    SW_EXPECT_FALSE( generated != generated );
    SW_EXPECT_FALSE( generated < generated );
}

/**
 * @brief [UUIDTest] 문자열 직렬화(toString) 및 역직렬화(tryParse) 라운드트립 검증
 */
SW_TEST_CASE( UUIDTest, StringFormattingAndParsing )
{
    const sw::UUID original = sw::UUID::generate();
    const string   str      = original.toString();

    // sw::UUID 정규 문자열 형식: 8-4-4-4-12 = 36자
    SW_EXPECT_EQUAL( 36u, str.length() );
    SW_EXPECT_EQUAL( '-', str[8] );
    SW_EXPECT_EQUAL( '-', str[13] );
    SW_EXPECT_EQUAL( '-', str[18] );
    SW_EXPECT_EQUAL( '-', str[23] );

    sw::UUID   parsed{};
    const bool parseSuccess = sw::UUID::tryParse( str, parsed );
    SW_EXPECT_TRUE( parseSuccess );
    SW_EXPECT_TRUE( original == parsed );
    SW_EXPECT_EQUAL( str, parsed.toString() );

    // Nil sw::UUID 직렬화 및 역직렬화
    const sw::UUID nilUUID{};
    const string   nilStr = nilUUID.toString();
    SW_EXPECT_EQUAL( string( "00000000-0000-0000-0000-000000000000" ), nilStr );

    sw::UUID parsedNil{};
    SW_EXPECT_TRUE( sw::UUID::tryParse( nilStr, parsedNil ) );
    SW_EXPECT_TRUE( parsedNil.isNull() );
    SW_EXPECT_TRUE( nilUUID == parsedNil );
}

/**
 * @brief [UUIDTest] 잘못된 형식 문자열 파싱 실패 처리 검증
 */
SW_TEST_CASE( UUIDTest, ParseFailureCases )
{
    sw::UUID outUUID{};

    // 빈 문자열
    SW_EXPECT_FALSE( sw::UUID::tryParse( "", outUUID ) );

    // 잘못된 길이
    SW_EXPECT_FALSE( sw::UUID::tryParse( "00000000-0000-0000-0000", outUUID ) );
    SW_EXPECT_FALSE( sw::UUID::tryParse( "00000000-0000-0000-0000-0000000000000", outUUID ) );

    // 잘못된 하이픈 위치
    SW_EXPECT_FALSE( sw::UUID::tryParse( "000000000-000-0000-0000-000000000000", outUUID ) );
    SW_EXPECT_FALSE( sw::UUID::tryParse( "00000000_0000_0000_0000_000000000000", outUUID ) );

    // 비 16진수 문자 포함
    SW_EXPECT_FALSE( sw::UUID::tryParse( "00000000-0000-0000-0000-00000000000Z", outUUID ) );
    SW_EXPECT_FALSE( sw::UUID::tryParse( "g0000000-0000-0000-0000-000000000000", outUUID ) );
}

/**
 * @brief [UUIDTest] std::hash 컨테이너 키 활용 검증
 */
SW_TEST_CASE( UUIDTest, StdHashSupport )
{
    std::unordered_set<sw::UUID> uuidSet;

    constexpr int32 kNumUUIDs = 100;
    for ( int32 index = 0; index < kNumUUIDs; ++index )
    {
        const sw::UUID u = sw::UUID::generate();
        SW_EXPECT_TRUE( uuidSet.insert( u ).second );
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kNumUUIDs ), uuidSet.size() );
}
