#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Inherit/AssetRedirect.h"

using namespace sw;

/**
 * @brief [AssetRedirectTest] 정체성 대체 줄을 읽는다 — 모르는 원소 · GUID 가 아닌 값 · 자기 자신 · 같은 옛 GUID 를 두 번이면 오류다
 */
SW_TEST_CASE( AssetRedirectTest, IdentityRowsAreParsedAndChecked )
{
    const expected<vector<AssetIdentityRedirect>> rows = AssetRedirect::parseIdentityRows( "<Redirects>"
                                                                                           "<IdentityRedirect from=\"0000000000000000000000000000000a\" to=\"0000000000000000000000000000000b\"/>"
                                                                                           "</Redirects>",
                                                                                           "common/redirects.xml" );
    SW_REQUIRE_TRUE( rows.has_value() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, rows->size() );
    SW_EXPECT_TRUE( ( *rows )[0]._from == *AssetGUID::tryParse( "0000000000000000000000000000000a" ) );
    SW_EXPECT_TRUE( ( *rows )[0]._to == *AssetGUID::tryParse( "0000000000000000000000000000000b" ) );

    const utf8* const arrBad[] = {
        "<Redirects><IdentityRedirect from=\"common/a.xml\" to=\"0000000000000000000000000000000b\"/></Redirects>",
        "<Redirects><IdentityRedirect from=\"0000000000000000000000000000000a\" to=\"0000000000000000000000000000000a\"/></Redirects>",
        ( "<Redirects><IdentityRedirect from=\"0000000000000000000000000000000a\" to=\"0000000000000000000000000000000b\"/>"
          "<IdentityRedirect from=\"0000000000000000000000000000000a\" to=\"0000000000000000000000000000000c\"/></Redirects>" ),
    };
    for ( const utf8* pBad : arrBad )
    {
        SW_EXPECT_LOG( "AssetRedirect.IdentityRowInvalid" );
        SW_EXPECT_FALSE( AssetRedirect::parseIdentityRows( pBad, "common/redirects.xml" ).has_value() );
    }
    SW_EXPECT_LOG( "AssetRedirect.UnknownElement" );
    SW_EXPECT_FALSE( AssetRedirect::parseIdentityRows( "<Redirects><Redirect from=\"common/a.xml\" to=\"common/b.xml\"/></Redirects>", "common/redirects.xml" ).has_value() );
}

/**
 * @brief [AssetRedirectTest] 표의 자리는 도메인 뿌리 하나다 — `game/<팩>` 은 두 마디
 */
SW_TEST_CASE( AssetRedirectTest, TableIDIsTheDomainRoot )
{
    SW_EXPECT_EQUAL( string( "game/oldpack/redirects.xml" ), AssetRedirect::makeTableID( "game/oldpack/maps/a.scene.xml" ) );
    SW_EXPECT_EQUAL( string( "common/redirects.xml" ), AssetRedirect::makeTableID( "common/data/a.xml" ) );
    SW_EXPECT_TRUE( AssetRedirect::isTable( "game/oldpack/redirects.xml" ) );
    SW_EXPECT_FALSE( AssetRedirect::isTable( "game/oldpack/maps/a.scene.xml" ) );
}
