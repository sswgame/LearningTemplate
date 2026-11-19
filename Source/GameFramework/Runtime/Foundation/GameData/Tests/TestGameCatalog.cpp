// GameCatalogTest — 카탈로그 키(`CatalogKey<TDef>`)로 찾기 · 반드시 있어야 하는 키(`require`)는 빈 키 · 없는 키를 오류로 멈춘다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"

using namespace sw;

namespace
{
    struct GameCatalogTestDef
    {
        hashed_string _id{};
        int32         _value{ 0 };
    };
} // namespace

/** @brief [GameCatalogTest] 키로 찾으면 id 로 찾은 것과 같고, 시작할 때 푸는 `require` 는 빈 키 · 없는 키를 `NotFound` 로 돌려준다(다른 항목으로 대신하지 않는다) */
SW_TEST_CASE( GameCatalogTest, KeyLookupAndRequire )
{
    GameCatalog<GameCatalogTestDef> catalog;
    (void)catalog.add( GameCatalogTestDef{ hashed_string( "fire" ), 3 } );
    const CatalogKey<GameCatalogTestDef> fire( hashed_string( "fire" ) );
    SW_REQUIRE_TRUE( catalog.find( fire ) != nullptr );
    SW_EXPECT_EQUAL( 3, catalog.find( fire )->_value );
    const expected<const GameCatalogTestDef*> found = catalog.require( fire );
    SW_REQUIRE_TRUE( found.has_value() );
    SW_EXPECT_TRUE( *found == catalog.find( fire ) );

    const expected<const GameCatalogTestDef*> missing = catalog.require( CatalogKey<GameCatalogTestDef>( hashed_string( "ice" ) ) );
    SW_REQUIRE_FALSE( missing.has_value() );
    SW_EXPECT_TRUE( missing.error().kind() == ErrorKind::NotFound );
    SW_EXPECT_FALSE( catalog.require( CatalogKey<GameCatalogTestDef>{} ).has_value() );
}
