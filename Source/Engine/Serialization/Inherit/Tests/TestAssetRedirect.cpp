#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Inherit/AssetRedirect.h"

namespace
{
    /** @brief 표 글 하나를 읽습니다. */
    bool loadOne( const utf8* pText )
    {
        return sw::AssetRedirect::loadFromXMLTexts( sw::vector<sw::string>{ pText }, sw::vector<sw::string>{ "test/redirects.xml" } );
    }
} // namespace

/**
 * @brief [AssetRedirectTest] 옛 id 가 끝 id 로 풀리고 사슬은 접힌다
 * @details 두 번 옮긴 에셋(A→B, B→C)의 첫 이름도 세이브 · 서버에 남아 있을 수 있다 — 한 번에 끝(C)으로 풀어야 한다(NEXT 5-5 R1).
 */
SW_TEST_CASE( AssetRedirectTest, ResolvesOldIdsAndCollapsesChains )
{
    SW_REQUIRE_TRUE( loadOne( "<Redirects>"
                              "<Redirect from=\"common/data/a.xml\" to=\"common/data/b.xml\"/>"
                              "<Redirect from=\"common/data/b.xml\" to=\"common/data/c.xml\"/>"
                              "</Redirects>" ) );
    sw::string resolved;
    SW_REQUIRE_TRUE( sw::AssetRedirect::resolve( "common/data/a.xml", resolved ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/c.xml" ), resolved );
    SW_REQUIRE_TRUE( sw::AssetRedirect::resolve( "common/data/b.xml", resolved ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/c.xml" ), resolved );
    SW_EXPECT_FALSE( sw::AssetRedirect::resolve( "common/data/c.xml", resolved ) );
    sw::AssetRedirect::clear();
    SW_EXPECT_FALSE( sw::AssetRedirect::resolve( "common/data/a.xml", resolved ) );
}

/**
 * @brief [AssetRedirectTest] 틀린 표는 로드 오류이고 지금 표를 바꾸지 않는다
 */
SW_TEST_CASE( AssetRedirectTest, MalformedTablesAreLoadErrors )
{
    SW_REQUIRE_TRUE( loadOne( "<Redirects><Redirect from=\"common/x.xml\" to=\"common/y.xml\"/></Redirects>" ) );

    SW_EXPECT_LOG( "AssetRedirect.Cycle" );
    SW_EXPECT_FALSE( loadOne( "<Redirects><Redirect from=\"common/p.xml\" to=\"common/q.xml\"/><Redirect from=\"common/q.xml\" to=\"common/p.xml\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.DuplicateFrom" );
    SW_EXPECT_FALSE( loadOne( "<Redirects><Redirect from=\"common/p.xml\" to=\"common/q.xml\"/><Redirect from=\"common/p.xml\" to=\"common/r.xml\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.SelfRedirect" );
    SW_EXPECT_FALSE( loadOne( "<Redirects><Redirect from=\"common/p.xml\" to=\"common/p.xml\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.NotCanonicalId" );
    SW_EXPECT_FALSE( loadOne( "<Redirects><Redirect from=\"Common/P.xml\" to=\"common/p.xml\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.UnknownElement" );
    SW_EXPECT_FALSE( loadOne( "<Redirects><Move from=\"common/p.xml\" to=\"common/q.xml\"/></Redirects>" ) );

    // 앞의 실패들은 표를 바꾸지 않았다.
    sw::string resolved;
    SW_REQUIRE_TRUE( sw::AssetRedirect::resolve( "common/x.xml", resolved ) );
    SW_EXPECT_EQUAL( sw::string( "common/y.xml" ), resolved );
    sw::AssetRedirect::clear();
}

/**
 * @brief [AssetRedirectTest] 옮기기가 표에 줄을 더하고, 되돌린 옮기기는 순환이 아니라 줄을 바꾼다
 * @details 에디터 옮기기 · move-asset 이 쓰는 규칙이다(NEXT 5-5 R1). 옮긴 자리를 옛 id 로 둔 줄을 남기면 B→A 가 A→B 와 고리가 되어 기동이 멈춘다.
 */
SW_TEST_CASE( AssetRedirectTest, MovesAddLinesAndMovingBackReplacesThem )
{
    SW_EXPECT_EQUAL( sw::string( "game/empty/redirects.xml" ), sw::AssetRedirect::makeTableID( "game/empty/maps/a.scene.xml" ) );
    SW_EXPECT_EQUAL( sw::string( "common/redirects.xml" ), sw::AssetRedirect::makeTableID( "common/data/a.xml" ) );
    SW_EXPECT_TRUE( sw::AssetRedirect::isTable( "game/empty/redirects.xml" ) );
    SW_EXPECT_FALSE( sw::AssetRedirect::isTable( "game/empty/maps/a.scene.xml" ) );

    const sw::vector<sw::string>   listA{ "common/data/a.xml" };
    const sw::vector<sw::string>   listB{ "common/data/b.xml" };
    const sw::vector<sw::string>   listC{ "common/data/c.xml" };
    const sw::expected<sw::string> first = sw::AssetRedirect::addToXMLText( sw::AssetRedirect::kEmptyTableText, "common/redirects.xml", listA, listB );
    SW_REQUIRE_TRUE( first.has_value() );
    SW_EXPECT_EQUAL( sw::string( "<Redirects>\n\t<Redirect from=\"common/data/a.xml\" to=\"common/data/b.xml\" />\n</Redirects>\n" ), *first );

    // 두 번째 옮기기는 사슬로 붙는다 — 읽을 때 A 도 C 로 풀린다.
    const sw::expected<sw::string> chained = sw::AssetRedirect::addToXMLText( *first, "common/redirects.xml", listB, listC );
    SW_REQUIRE_TRUE( chained.has_value() );
    SW_REQUIRE_TRUE( loadOne( chained->c_str() ) );
    sw::string resolved;
    SW_REQUIRE_TRUE( sw::AssetRedirect::resolve( "common/data/a.xml", resolved ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/c.xml" ), resolved );

    // C 를 A 로 되돌리면 A 를 옛 id 로 둔 줄이 빠진다 — A 에 다시 에셋이 산다.
    const sw::expected<sw::string> back = sw::AssetRedirect::addToXMLText( *chained, "common/redirects.xml", listC, listA );
    SW_REQUIRE_TRUE( back.has_value() );
    SW_REQUIRE_TRUE( loadOne( back->c_str() ) );
    SW_EXPECT_FALSE( sw::AssetRedirect::resolve( "common/data/a.xml", resolved ) );
    SW_REQUIRE_TRUE( sw::AssetRedirect::resolve( "common/data/b.xml", resolved ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/a.xml" ), resolved );

    // 틀린 표에는 더하지 않고, 읽히지 않을 줄은 쓰지 않는다.
    SW_EXPECT_LOG( "AssetRedirect.WrongRoot" );
    SW_EXPECT_FALSE( sw::AssetRedirect::addToXMLText( "<Moves/>", "common/redirects.xml", listA, listB ).has_value() );
    SW_EXPECT_LOG( "AssetRedirect.NotCanonicalId" );
    SW_EXPECT_FALSE( sw::AssetRedirect::addToXMLText( sw::AssetRedirect::kEmptyTableText, "common/redirects.xml", sw::vector<sw::string>{ "Common/A.xml" }, listB ).has_value() );
    sw::AssetRedirect::clear();
}
