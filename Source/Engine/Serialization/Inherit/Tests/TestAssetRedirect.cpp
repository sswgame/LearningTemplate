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
