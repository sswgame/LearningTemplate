#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Inherit/AssetRedirect.h"

using namespace sw;

namespace
{
    struct AssetRedirectTestUtil
    {
        /** @brief 표 글 하나를 프로세스의 옛 경로 표로 읽습니다. */
        static bool loadOne( const utf8* pText ) { return AssetRedirect::loadFromXMLTexts( vector<string>{ pText }, vector<string>{ "test/redirects.xml" } ); }

        /** @brief 저장된 기록의 글 @p pText 를 푼 경로입니다. 풀지 못하면 빈 글입니다. */
        static string readSaved( const utf8* pText )
        {
            const expected<AssetReferenceTarget> target = AssetRedirect::readSavedReference( pText );
            return target.has_value() ? target->_path : string{};
        }

        /** @brief 프로세스 색인을 줄 하나로 바꾸고 있던 색인을 돌려줍니다 — 부른 쪽이 되돌린다. */
        static AssetRegistry installOne( const AssetGUID& guid, const utf8* pAssetID )
        {
            return AssetRegistry::install( std::move( *AssetRegistry::merge( vector<AssetRegistryLayer>{
                AssetRegistryLayer{ "one", { AssetRegistryEntry{ guid, pAssetID } } }
            } ) ) );
        }
    };
} // namespace

/**
 * @brief [AssetRedirectTest] 저장된 기록의 옛 경로는 정확한 줄 · 폴더 접두 줄로 한 번 풀리고(사슬은 접는다) 지금 에셋의 GUID 를 얻는다 — GUID 글은 색인만 본다
 */
SW_TEST_CASE( AssetRedirectTest, SavedPathsResolveOnceToTheAssetTheyNameNow )
{
    using Util               = AssetRedirectTestUtil;
    const AssetGUID guid     = *AssetGUID::tryParse( "0000000000000000000000000000ed01" );
    AssetRegistry   previous = Util::installOne( guid, "game/newpack/maps/new.scene.xml" );
    SW_REQUIRE_TRUE( Util::loadOne( "<Redirects>"
                                    "<PrefixRedirect from=\"game/oldpack\" to=\"game/newpack\"/>"
                                    "<Redirect from=\"game/oldpack/maps/kept.scene.xml\" to=\"game/newpack/maps/exact.scene.xml\"/>"
                                    "<Redirect from=\"game/newpack/maps/old.scene.xml\" to=\"game/newpack/maps/mid.scene.xml\"/>"
                                    "<Redirect from=\"game/newpack/maps/mid.scene.xml\" to=\"game/newpack/maps/new.scene.xml\"/>"
                                    "</Redirects>" ) );
    SW_EXPECT_EQUAL( string( "game/newpack/maps/a.scene.xml" ), Util::readSaved( "game/oldpack/maps/a.scene.xml" ) );
    SW_EXPECT_EQUAL( string( "game/newpack/maps/exact.scene.xml" ), Util::readSaved( "game/oldpack/maps/kept.scene.xml" ) ); // 정확한 줄이 먼저다
    SW_EXPECT_EQUAL( string( "game/newpack/maps/new.scene.xml" ), Util::readSaved( "game/oldpack/maps/old.scene.xml" ) );    // 팩 안에서 옮긴 뒤 팩을 옮긴 사슬
    SW_EXPECT_EQUAL( string( "game/oldpackish/a.xml" ), Util::readSaved( "game/oldpackish/a.xml" ) );                        // 이름이 겹치기만 한 폴더
    SW_EXPECT_EQUAL( string( "game/newpack/maps/new.scene.xml" ), Util::readSaved( "guid:0000000000000000000000000000ed01" ) );
    const expected<AssetReferenceTarget> target = AssetRedirect::readSavedReference( "game/oldpack/maps/old.scene.xml" );
    SW_REQUIRE_TRUE( target.has_value() );
    SW_EXPECT_TRUE( target->_guid == guid ); // 다시 저장하면 GUID 로 적힌다
    AssetRedirect::clear();
    SW_EXPECT_EQUAL( string( "game/oldpack/maps/a.scene.xml" ), Util::readSaved( "game/oldpack/maps/a.scene.xml" ) );
    (void)AssetRegistry::install( std::move( previous ) );
}

/**
 * @brief [AssetRedirectTest] 정체성 대체 줄만 골라 읽는다 — GUID 가 아니거나 자기 자신 · 같은 옛 GUID 를 두 번이면 오류다
 */
SW_TEST_CASE( AssetRedirectTest, IdentityRowsAreParsedApartFromSavedPaths )
{
    const expected<vector<AssetIdentityRedirect>> rows = AssetRedirect::parseIdentityRows( "<Redirects>"
                                                                                           "<Redirect from=\"common/a.xml\" to=\"common/b.xml\"/>"
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
}

/**
 * @brief [AssetRedirectTest] 틀린 표는 로드 오류이고 지금 표를 바꾸지 않는다
 */
SW_TEST_CASE( AssetRedirectTest, MalformedTablesAreLoadErrors )
{
    using Util = AssetRedirectTestUtil;
    SW_REQUIRE_TRUE( Util::loadOne( "<Redirects><Redirect from=\"common/x.xml\" to=\"common/y.xml\"/></Redirects>" ) );

    SW_EXPECT_LOG( "AssetRedirect.Cycle" );
    SW_EXPECT_FALSE( Util::loadOne( "<Redirects><Redirect from=\"common/p.xml\" to=\"common/q.xml\"/><Redirect from=\"common/q.xml\" to=\"common/p.xml\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.DuplicateFrom" );
    SW_EXPECT_FALSE( Util::loadOne( "<Redirects><Redirect from=\"common/p.xml\" to=\"common/q.xml\"/><Redirect from=\"common/p.xml\" to=\"common/r.xml\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.DuplicateFrom" );
    SW_EXPECT_FALSE( Util::loadOne( "<Redirects><PrefixRedirect from=\"game/a\" to=\"game/b\"/><PrefixRedirect from=\"game/a\" to=\"game/c\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.SelfRedirect" );
    SW_EXPECT_FALSE( Util::loadOne( "<Redirects><Redirect from=\"common/p.xml\" to=\"common/p.xml\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.NotCanonicalId" );
    SW_EXPECT_FALSE( Util::loadOne( "<Redirects><Redirect from=\"Common/P.xml\" to=\"common/p.xml\"/></Redirects>" ) );
    SW_EXPECT_LOG( "AssetRedirect.UnknownElement" );
    SW_EXPECT_FALSE( Util::loadOne( "<Redirects><Move from=\"common/p.xml\" to=\"common/q.xml\"/></Redirects>" ) );

    SW_EXPECT_EQUAL( string( "common/y.xml" ), Util::readSaved( "common/x.xml" ) ); // 앞의 실패들은 표를 바꾸지 않았다
    AssetRedirect::clear();

    SW_EXPECT_EQUAL( string( "game/oldpack/redirects.xml" ), AssetRedirect::makeTableID( "game/oldpack/maps/a.scene.xml" ) );
    SW_EXPECT_EQUAL( string( "common/redirects.xml" ), AssetRedirect::makeTableID( "common/data/a.xml" ) );
    SW_EXPECT_TRUE( AssetRedirect::isTable( "game/oldpack/redirects.xml" ) );
    SW_EXPECT_FALSE( AssetRedirect::isTable( "game/oldpack/maps/a.scene.xml" ) );
}
