#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Pack/PackTypes.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/TypeRegistry.h"

using namespace sw;

// EnumNameTest — 리플렉션 `ENUM()` 열거자 이름: 빈틈없는 열거형(목록 인덱스) · 빈틈 있는 열거형(맵), 범위 밖 · 등록 안 된 열거형의 대신 이름,
// 찾기(`findEnumName`)의 nullptr, 이름 → 값(비트플래그 `A|B` 포함). 엔진의 PackTypes 열거형을 쓴다(nogpu).

/**
 * @brief [EnumNameTest] 빈틈없는 열거형은 값마다 열거자 이름이고, 범위 밖은 `Unknown` 이다
 */
SW_TEST_CASE( EnumNameTest, DenseEnumNamesByIndex )
{
    SW_EXPECT_EQUAL( string( "Sorted" ), string( getEnumName( PackLayoutMode::Sorted ) ) );
    SW_EXPECT_EQUAL( string( "Slot" ), string( getEnumName( PackLayoutMode::Slot ) ) );
    SW_EXPECT_EQUAL( string( EnumNameCache::kUnknownName ), string( getEnumName( static_cast<PackLayoutMode>( 2 ) ) ) );
}

/**
 * @brief [EnumNameTest] 빈틈 있는 열거형(None..Zstd, Custom = 255)은 적은 값만 이름이 있고, 빈 값은 `Unknown` · 찾기는 nullptr 이다
 */
SW_TEST_CASE( EnumNameTest, SparseEnumNamesOnlyDeclaredValues )
{
    SW_EXPECT_EQUAL( string( "Zlib" ), string( getEnumName( PackCompressionKind::Zlib ) ) );
    SW_EXPECT_EQUAL( string( "Custom" ), string( getEnumName( PackCompressionKind::Custom ) ) );
    SW_EXPECT_EQUAL( string( EnumNameCache::kUnknownName ), string( getEnumName( static_cast<PackCompressionKind>( 7 ) ) ) );

    const hashed_string& fqn  = typeFqn<PackCompressionKind>();
    const utf8*          pLZ4 = findEnumName( fqn, 3 );
    SW_REQUIRE_TRUE( pLZ4 != nullptr );
    SW_EXPECT_EQUAL( string( "LZ4" ), string( pLZ4 ) );
    SW_EXPECT_TRUE( findEnumName( fqn, 7 ) == nullptr );
    SW_EXPECT_EQUAL( string( "LZ4" ), string( getEnumName( fqn, 3 ) ) );
}

/**
 * @brief [EnumNameTest] 등록되지 않은 열거형 이름은 찾기가 nullptr, 표시용은 `Unknown` 이다
 */
SW_TEST_CASE( EnumNameTest, UnregisteredEnumHasNoNames )
{
    const hashed_string unknownFqn( "sw::EnumNameTestNoSuchEnum" );
    SW_EXPECT_TRUE( findEnumName( unknownFqn, 0 ) == nullptr );
    SW_EXPECT_EQUAL( string( EnumNameCache::kUnknownName ), string( getEnumName( unknownFqn, 0 ) ) );
}

/**
 * @brief [EnumNameTest] 이름 → 값은 열거자 철자 그대로만 받고, 비트플래그는 `A|B` 를 합친 값으로 읽는다
 */
SW_TEST_CASE( EnumNameTest, NamesParseBackToValues )
{
    const optional<PackCompressionKind> codec = tryParseEnum<PackCompressionKind>( "Zstd" );
    SW_REQUIRE_TRUE( codec.has_value() );
    SW_EXPECT_TRUE( *codec == PackCompressionKind::Zstd );
    SW_EXPECT_FALSE( tryParseEnum<PackCompressionKind>( "Brotli" ).has_value() );
    SW_EXPECT_FALSE( tryParseEnum<PackCompressionKind>( "zstd" ).has_value() ); // 철자 그대로만 — 대소문자가 다르면 모르는 이름
    SW_EXPECT_FALSE( tryParseEnum<PackCompressionKind>( "" ).has_value() );

    const optional<PackFlag> flags = tryParseEnum<PackFlag>( "HasStringPool|HasCRC32" );
    SW_REQUIRE_TRUE( flags.has_value() );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( *flags ) );
    SW_EXPECT_FALSE( tryParseEnum<PackFlag>( "HasStringPool|Bogus" ).has_value() ); // 아는 토큰만 남기지 않는다
    SW_EXPECT_EQUAL( string( "Encrypted" ), string( getEnumName( PackFlag::Encrypted ) ) );
}
