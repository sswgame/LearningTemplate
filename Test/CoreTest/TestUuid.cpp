#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Uuid/Uuid.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) UuidTest — 생성, 속성, 포맷팅 및 해시 검증
// ------------------------------------------------------------------------------

/**
 * @brief [UuidTest] UUID v4 생성 및 RFC 4122 규격 비트 검증
 */
SW_TEST_CASE( UuidTest, GenerateAndProperties )
{
	const Uuid uuid1 = Uuid::generate();
	const Uuid uuid2 = Uuid::generate();

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
 * @brief [UuidTest] Nil UUID 및 비교 연산자 검증
 */
SW_TEST_CASE( UuidTest, NullAndEquality )
{
	Uuid nilUuid{};
	SW_EXPECT_TRUE( nilUuid.isNull() );

	Uuid nilUuid2{};
	SW_EXPECT_TRUE( nilUuid == nilUuid2 );
	SW_EXPECT_FALSE( nilUuid != nilUuid2 );
	SW_EXPECT_FALSE( nilUuid < nilUuid2 );

	const Uuid generated = Uuid::generate();
	SW_EXPECT_FALSE( generated.isNull() );
	SW_EXPECT_TRUE( nilUuid != generated );
	SW_EXPECT_FALSE( nilUuid == generated );

	// 자기 자신과의 비교
	SW_EXPECT_TRUE( generated == generated );
	SW_EXPECT_FALSE( generated != generated );
	SW_EXPECT_FALSE( generated < generated );
}

/**
 * @brief [UuidTest] 문자열 직렬화(toString) 및 역직렬화(tryParse) 라운드트립 검증
 */
SW_TEST_CASE( UuidTest, StringFormattingAndParsing )
{
	const Uuid	 original = Uuid::generate();
	const string str	  = original.toString();

	// UUID 정규 문자열 형식: 8-4-4-4-12 = 36자
	SW_EXPECT_EQUAL( 36u, str.length() );
	SW_EXPECT_EQUAL( '-', str[8] );
	SW_EXPECT_EQUAL( '-', str[13] );
	SW_EXPECT_EQUAL( '-', str[18] );
	SW_EXPECT_EQUAL( '-', str[23] );

	Uuid	   parsed{};
	const bool parseSuccess = Uuid::tryParse( str, parsed );
	SW_EXPECT_TRUE( parseSuccess );
	SW_EXPECT_TRUE( original == parsed );
	SW_EXPECT_EQUAL( str, parsed.toString() );

	// Nil UUID 직렬화 및 역직렬화
	const Uuid	 nilUuid{};
	const string nilStr = nilUuid.toString();
	SW_EXPECT_EQUAL( string( "00000000-0000-0000-0000-000000000000" ), nilStr );

	Uuid parsedNil{};
	SW_EXPECT_TRUE( Uuid::tryParse( nilStr, parsedNil ) );
	SW_EXPECT_TRUE( parsedNil.isNull() );
	SW_EXPECT_TRUE( nilUuid == parsedNil );
}

/**
 * @brief [UuidTest] 잘못된 형식 문자열 파싱 실패 처리 검증
 */
SW_TEST_CASE( UuidTest, ParseFailureCases )
{
	Uuid outUuid{};

	// 빈 문자열
	SW_EXPECT_FALSE( Uuid::tryParse( "", outUuid ) );

	// 잘못된 길이
	SW_EXPECT_FALSE( Uuid::tryParse( "00000000-0000-0000-0000", outUuid ) );
	SW_EXPECT_FALSE( Uuid::tryParse( "00000000-0000-0000-0000-0000000000000", outUuid ) );

	// 잘못된 하이픈 위치
	SW_EXPECT_FALSE( Uuid::tryParse( "000000000-000-0000-0000-000000000000", outUuid ) );
	SW_EXPECT_FALSE( Uuid::tryParse( "00000000_0000_0000_0000_000000000000", outUuid ) );

	// 비 16진수 문자 포함
	SW_EXPECT_FALSE( Uuid::tryParse( "00000000-0000-0000-0000-00000000000Z", outUuid ) );
	SW_EXPECT_FALSE( Uuid::tryParse( "g0000000-0000-0000-0000-000000000000", outUuid ) );
}

/**
 * @brief [UuidTest] std::hash 컨테이너 키 활용 검증
 */
SW_TEST_CASE( UuidTest, StdHashSupport )
{
	std::unordered_set<Uuid> uuidSet;

	constexpr int32 kNumUuids = 100;
	for ( int32 index = 0; index < kNumUuids; ++index )
	{
		const Uuid u = Uuid::generate();
		SW_EXPECT_TRUE( uuidSet.insert( u ).second );
	}

	SW_EXPECT_EQUAL( static_cast<size_t>( kNumUuids ), uuidSet.size() );
}
