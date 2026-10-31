#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/Pack/PackTypes.h"
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
    SW_EXPECT_EQUAL( string( "Zlib" ), string( getEnumName( PackCompressionType::Zlib ) ) );
    SW_EXPECT_EQUAL( string( "Custom" ), string( getEnumName( PackCompressionType::Custom ) ) );
    SW_EXPECT_EQUAL( string( EnumNameCache::kUnknownName ), string( getEnumName( static_cast<PackCompressionType>( 7 ) ) ) );

    const hashed_string& fqn  = typeFqn<PackCompressionType>();
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
    const TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();

    PackCompressionType codec = PackCompressionType::None;
    SW_EXPECT_TRUE( registry.enumFromString( "Zstd", codec ) );
    SW_EXPECT_TRUE( codec == PackCompressionType::Zstd );
    SW_EXPECT_FALSE( registry.enumFromString( "Brotli", codec ) );

    PackFlag flags = PackFlag::None;
    SW_EXPECT_TRUE( registry.enumFromString( "HasStringPool|HasCRC32", flags ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( flags ) );
    SW_EXPECT_EQUAL( string( "Encrypted" ), string( getEnumName( PackFlag::Encrypted ) ) );
}
