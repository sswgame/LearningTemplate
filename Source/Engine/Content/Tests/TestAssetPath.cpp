#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/AssetPath.h"

// 리소스 경로 정규화와 도메인 표의 최장 일치 가르기.

using namespace sw;

SW_TEST_CASE( AssetPathTest, NormalizesAndRejectsEscapes )
{
    const optional<AssetPath> path = AssetPath::tryMake( "Common", ".\\Textures//Rock.DDS" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_EQUAL( string( "common" ), path->domain() );
    SW_EXPECT_EQUAL( string( "textures/rock.dds" ), path->relative() );

    SW_EXPECT_FALSE( AssetPath::tryMake( "", "" ).has_value() );
    SW_EXPECT_FALSE( AssetPath::tryMake( "", "../x" ).has_value() );
    SW_EXPECT_FALSE( AssetPath::tryMake( "", "a/../x" ).has_value() );
    SW_EXPECT_FALSE( AssetPath::tryMake( "", "/x" ).has_value() );
    SW_EXPECT_FALSE( AssetPath::tryMake( "", "C:/x" ).has_value() );
}

SW_TEST_CASE( DomainTableTest, SplitUsesLongestRegisteredDomain )
{
    DomainTable table;
    SW_REQUIRE_TRUE( table.add( "engine", 2, true ) );
    SW_REQUIRE_TRUE( table.add( "game/playground", 0, true ) );
    SW_REQUIRE_TRUE( table.add( "game", 5, true ) );
    SW_EXPECT_FALSE( table.add( "Engine", 9, true ) ); // 글자만 다른 같은 이름

    const optional<AssetPath> deep = table.split( "Game/Playground/maps/a.scene" );
    SW_REQUIRE_TRUE( deep.has_value() );
    SW_EXPECT_EQUAL( string( "game/playground" ), deep->domain() );
    SW_EXPECT_EQUAL( string( "maps/a.scene" ), deep->relative() );

    const optional<AssetPath> shallow = table.split( "game/other/x" );
    SW_REQUIRE_TRUE( shallow.has_value() );
    SW_EXPECT_EQUAL( string( "game" ), shallow->domain() );

    // 도메인 이름을 앞에 둔 다른 글자(`engineer/x`)는 도메인이 아니다.
    const optional<AssetPath> none = table.split( "engineer/x" );
    SW_REQUIRE_TRUE( none.has_value() );
    SW_EXPECT_FALSE( none->hasDomain() );

    SW_EXPECT_EQUAL( string( "game/playground" ), table.listByPriority().front()->_name );
}
