// CatalogKeyTest — 카탈로그 키 칸(`CatalogKey<Tag>`): 다른 종류 사이 변환 없음, 글 처리기("CatalogKey")의 읽기 · 쓰기, 빈 값.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Serializer/CatalogKey.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include <type_traits>

using namespace sw;

namespace
{
    struct CatalogKeyTestSpellDef
    {
    };
    struct CatalogKeyTestUnitDef
    {
    };

    using SpellKey = CatalogKey<CatalogKeyTestSpellDef>;
    using UnitKey  = CatalogKey<CatalogKeyTestUnitDef>;

    // 다른 카탈로그의 키는 넣을 수 없다 — 변환도, 생성도, 대입도 컴파일되지 않는다. 글 · 이름에서 만들 때는 explicit 이다.
    static_assert( std::is_convertible_v<UnitKey, SpellKey> == false );
    static_assert( std::is_constructible_v<SpellKey, UnitKey> == false );
    static_assert( std::is_assignable_v<SpellKey&, UnitKey> == false );
    static_assert( std::is_convertible_v<hashed_string, SpellKey> == false );
    static_assert( std::is_constructible_v<SpellKey, hashed_string> );
    static_assert( sizeof( SpellKey ) == sizeof( hashed_string ) );
} // namespace

/** @brief [CatalogKeyTest] 글 처리기는 id 글을 그대로 읽고 쓴다 — 앞뒤 공백 · 따옴표는 빼고, 빈 글은 키를 비운다 */
SW_TEST_CASE( CatalogKeyTest, TextHandlerReadsAndWritesTheID )
{
    const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
    const hashed_string     typeName( CatalogKeyBase::kTypeName );
    SpellKey                key;
    SW_EXPECT_TRUE( key.empty() );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &key }, typeName, "  fireball \t", context ) );
    SW_EXPECT_TRUE( key == SpellKey( hashed_string( "fireball" ) ) );
    SW_EXPECT_EQUAL( string( "fireball" ), SerializerUtil::formatValueText( ValueAddress{ &key }, typeName, context ) );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &key }, typeName, "\"ice\"", context ) );
    SW_EXPECT_TRUE( key.key() == hashed_string( "ice" ) );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &key }, typeName, "", context ) );
    SW_EXPECT_TRUE( key.empty() );
    SW_EXPECT_EQUAL( string(), SerializerUtil::formatValueText( ValueAddress{ &key }, typeName, context ) );
}

/** @brief [CatalogKeyTest] 빈 키는 이름 형식처럼 "없음" 이다 — 필수 칸(`Required`)의 빈 값 판정이 본다 */
SW_TEST_CASE( CatalogKeyTest, EmptyKeyIsANameValue )
{
    SW_EXPECT_TRUE( SerializerUtil::isNameValueType( hashed_string( CatalogKeyBase::kTypeName ) ) );
}
