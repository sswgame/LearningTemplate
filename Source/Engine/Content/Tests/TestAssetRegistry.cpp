#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetRegistry.h"

using namespace sw;

namespace
{
    struct AssetRegistryTestUtil
    {
        static AssetGUID makeGUID( const utf8* pText ) { return *AssetGUID::tryParse( pText ); }

        static AssetRegistryLayer makeLayer( const utf8* pName, const vector<AssetRegistryEntry>& listEntry ) { return AssetRegistryLayer{ pName, listEntry }; }
    };
} // namespace

/**
 * @brief [AssetRegistryTest] 마운트 순서로 합친 색인에서 뒤 층(패치 · DLC)이 이긴다 — 옮긴 에셋은 새 경로로 풀리고, 옛 경로는 다른 에셋을 가리키지 않는다
 */
SW_TEST_CASE( AssetRegistryTest, LaterLayerWinsAndMovedAssetLeavesNoStalePath )
{
    using Util               = AssetRegistryTestUtil;
    const AssetGUID moved    = Util::makeGUID( "00000000000000000000000000000001" );
    const AssetGUID kept     = Util::makeGUID( "00000000000000000000000000000002" );
    const AssetGUID occupant = Util::makeGUID( "00000000000000000000000000000003" );
    const AssetGUID lost     = Util::makeGUID( "00000000000000000000000000000004" );

    vector<AssetRegistryLayer> listLayer;
    listLayer.push_back( Util::makeLayer( "engine/assetindex.xml", {
                                                                       {moved,   "engine/a/old.xml"},
                                                                       { kept,  "engine/a/kept.xml"},
                                                                       { lost, "engine/a/taken.xml"}
    } ) );
    listLayer.push_back( Util::makeLayer( "patch", {
                                                       {   moved,   "engine/b/new.xml"},
                                                       {occupant, "engine/a/taken.xml"}
    } ) );
    const expected<AssetRegistry> registry = AssetRegistry::merge( listLayer );
    SW_REQUIRE_TRUE( registry.has_value() );

    SW_EXPECT_EQUAL( string( "engine/b/new.xml" ), registry->findPath( moved ).value_or( string{} ) );
    SW_EXPECT_FALSE( registry->findGUID( "engine/a/old.xml" ).has_value() ); // 옛 자리는 비었다
    SW_EXPECT_TRUE( registry->findGUID( "ENGINE/A/KEPT.XML" ) == kept );     // 경로 찾기는 대소문자를 가리지 않는다
    SW_EXPECT_TRUE( registry->findGUID( "engine/a/taken.xml" ) == occupant );
    SW_EXPECT_FALSE( registry->findPath( lost ).has_value() ); // 자리를 뺏긴 에셋은 풀리지 않는다(다른 파일을 가리키지 않는다)
    SW_EXPECT_EQUAL( size_t{ 3 }, registry->getCount() );
}

/**
 * @brief [AssetRegistryTest] 한 층 안에서 GUID · 경로가 겹치면 합치지 않는다 — 고르지 않고 오류다
 */
SW_TEST_CASE( AssetRegistryTest, RepeatedGUIDOrPathInOneLayerIsAnError )
{
    using Util           = AssetRegistryTestUtil;
    const AssetGUID guid = Util::makeGUID( "0000000000000000000000000000000a" );
    {
        vector<AssetRegistryLayer> listLayer;
        listLayer.push_back( Util::makeLayer( "one", {
                                                         {guid, "engine/a.xml"},
                                                         {guid, "engine/b.xml"}
        } ) );
        SW_EXPECT_FALSE( AssetRegistry::merge( listLayer ).has_value() );
    }
    {
        vector<AssetRegistryLayer> listLayer;
        listLayer.push_back( Util::makeLayer( "one", {
                                                         {                                                guid, "engine/a.xml"},
                                                         {Util::makeGUID( "0000000000000000000000000000000b" ), "Engine/A.xml"}
        } ) );
        SW_EXPECT_FALSE( AssetRegistry::merge( listLayer ).has_value() );
    }
}

/**
 * @brief [AssetRegistryTest] 참조 글 표기는 `guid:<소문자 32 자>` 하나다 — 다른 철자는 GUID 표기가 아니거나 틀린 표기다
 */
SW_TEST_CASE( AssetRegistryTest, ReferenceTextHasOneSpelling )
{
    const AssetGUID guid = AssetRegistryTestUtil::makeGUID( "0123456789abcdef0123456789abcdef" );
    SW_EXPECT_EQUAL( string( "guid:0123456789abcdef0123456789abcdef" ), AssetRegistry::makeReferenceText( guid ) );
    SW_EXPECT_TRUE( AssetRegistry::isReferenceText( "guid:0123456789abcdef0123456789abcdef" ) );
    SW_EXPECT_TRUE( AssetRegistry::parseReferenceText( "guid:0123456789abcdef0123456789abcdef" ) == guid );
    SW_EXPECT_FALSE( AssetRegistry::isReferenceText( "engine/a.xml" ) );
    SW_EXPECT_TRUE( AssetRegistry::isReferenceText( "guid:NOPE" ) ); // 꼴은 GUID 참조인데 읽지 못한다 — 부르는 쪽이 오류로 멈춘다
    SW_EXPECT_FALSE( AssetRegistry::parseReferenceText( "guid:NOPE" ).has_value() );
    SW_EXPECT_FALSE( AssetRegistry::parseReferenceText( "guid:0123456789ABCDEF0123456789ABCDEF" ).has_value() );
}

/**
 * @brief [AssetRegistryTest] 프로세스 표에 건 색인으로 GUID ↔ 경로를 풀고, 빈 색인을 걸면 풀리지 않는다
 */
SW_TEST_CASE( AssetRegistryTest, InstalledTableResolvesBothWays )
{
    const AssetGUID            guid = AssetRegistryTestUtil::makeGUID( "000000000000000000000000000000c1" );
    vector<AssetRegistryLayer> listLayer;
    listLayer.push_back( AssetRegistryTestUtil::makeLayer( "one", {
                                                                      { guid, "common/data/x.xml" }
    } ) );
    expected<AssetRegistry> registry = AssetRegistry::merge( listLayer );
    SW_REQUIRE_TRUE( registry.has_value() );
    AssetRegistry previous = AssetRegistry::install( std::move( *registry ) ); // 기동이 건 표 — 끝에 되돌린다
    SW_EXPECT_EQUAL( string( "common/data/x.xml" ), AssetRegistry::resolve( guid ).value_or( string{} ) );
    SW_EXPECT_TRUE( AssetRegistry::resolveGUID( "common/data/x.xml" ) == guid );
    (void)AssetRegistry::install( AssetRegistry{} );
    SW_EXPECT_FALSE( AssetRegistry::resolve( guid ).has_value() );
    (void)AssetRegistry::install( std::move( previous ) );
}

/**
 * @brief [AssetRegistryTest] 개발 실행 중 옮기기 · 새 정체성이 프로세스 표를 고친다 — 옮긴 에셋은 새 경로로 풀리고 옛 경로에는 GUID 가 없다
 */
SW_TEST_CASE( AssetRegistryTest, MoveAndRecordUpdateTheInstalledTable )
{
    const AssetGUID            moved   = AssetRegistryTestUtil::makeGUID( "000000000000000000000000000000d1" );
    const AssetGUID            created = AssetRegistryTestUtil::makeGUID( "000000000000000000000000000000d2" );
    vector<AssetRegistryLayer> listLayer;
    listLayer.push_back( AssetRegistryTestUtil::makeLayer( "one", {
                                                                      { moved, "common/data/old.xml" }
    } ) );
    AssetRegistry previous = AssetRegistry::install( std::move( *AssetRegistry::merge( listLayer ) ) );

    SW_EXPECT_TRUE( AssetRegistry::move( "common/data/old.xml", "common/data/sub/new.xml" ) );
    SW_EXPECT_EQUAL( string( "common/data/sub/new.xml" ), AssetRegistry::resolve( moved ).value_or( string{} ) );
    SW_EXPECT_FALSE( AssetRegistry::resolveGUID( "common/data/old.xml" ).has_value() );
    SW_EXPECT_FALSE( AssetRegistry::move( "common/data/unknown.xml", "common/data/x.xml" ) );

    AssetRegistry::record( created, "common/data/created.xml" );
    SW_EXPECT_TRUE( AssetRegistry::resolveGUID( "common/data/created.xml" ) == created );

    (void)AssetRegistry::install( std::move( previous ) );
}

/**
 * @brief [AssetRegistryTest] 참조 글 읽기 하나 — GUID 글은 지금 경로로, 경로 글은 정체성을 찾아, 색인에 없는 경로는 경로만, 틀린 글 · 없는 GUID 는 까닭이 다른 오류다. 쓰기는 그 짝이다
 */
SW_TEST_CASE( AssetRegistryTest, ReferenceTextReadsAndWritesOneWay )
{
    using Util                             = AssetRegistryTestUtil;
    const AssetGUID               guid     = Util::makeGUID( "0000000000000000000000000000f00d" );
    const expected<AssetRegistry> registry = AssetRegistry::merge( vector<AssetRegistryLayer>{
        Util::makeLayer( "one", { { guid, "engine/shaders/toon.hlsl" } }
          )
    } );
    SW_REQUIRE_TRUE( registry.has_value() );
    AssetRegistry previous = AssetRegistry::install( *registry );

    const expected<AssetReferenceTarget> byGUID    = AssetRegistry::readReferenceText( " guid:0000000000000000000000000000f00d " );
    const expected<AssetReferenceTarget> byPath    = AssetRegistry::readReferenceText( "engine/shaders/toon.hlsl" );
    const expected<AssetReferenceTarget> unindexed = AssetRegistry::readReferenceText( "game/playground" );
    const expected<AssetReferenceTarget> empty     = AssetRegistry::readReferenceText( "" );
    SW_EXPECT_TRUE( AssetRegistry::readReferenceText( "guid:NOPE" ).error().kind() == ErrorKind::InvalidArgument );
    SW_EXPECT_TRUE( AssetRegistry::readReferenceText( "guid:0000000000000000000000000000dead" ).error().kind() == ErrorKind::NotFound );
    (void)AssetRegistry::install( std::move( previous ) );

    SW_REQUIRE_TRUE( byGUID.has_value() && byPath.has_value() && unindexed.has_value() && empty.has_value() );
    SW_EXPECT_EQUAL( string( "engine/shaders/toon.hlsl" ), byGUID->_path );
    SW_EXPECT_TRUE( byPath->_guid == guid );
    SW_EXPECT_FALSE( unindexed->_guid.has_value() );
    SW_EXPECT_TRUE( empty->_path.empty() && empty->_guid.has_value() == false );
    SW_EXPECT_EQUAL( string( "guid:0000000000000000000000000000f00d" ), AssetRegistry::makeReferenceText( byPath->_path, byPath->_guid ) );
    SW_EXPECT_EQUAL( string( "game/playground" ), AssetRegistry::makeReferenceText( unindexed->_path, unindexed->_guid ) );
}

/**
 * @brief [AssetRegistryTest] 정체성 대체 줄은 그 GUID 의 에셋이 없을 때만 따라간다(사슬은 접는다) — 살아 있는 에셋이 먼저이고, 푼 참조는 남은 쪽의 GUID 로 다시 쓰인다. 순환은 합치기 오류다
 */
SW_TEST_CASE( AssetRegistryTest, IdentityRedirectsFollowOnlyMissingAssets )
{
    using Util              = AssetRegistryTestUtil;
    const AssetGUID merged  = Util::makeGUID( "0000000000000000000000000000fe01" );
    const AssetGUID between = Util::makeGUID( "0000000000000000000000000000fe02" );
    const AssetGUID kept    = Util::makeGUID( "0000000000000000000000000000fe03" );
    const AssetGUID alive   = Util::makeGUID( "0000000000000000000000000000fe04" );

    AssetRegistryLayer layer = Util::makeLayer( "one", {
                                                           { kept,  "common/kept.xml"},
                                                           {alive, "common/alive.xml"}
    } );
    layer._listRedirect      = {
        AssetIdentityRedirect{ merged, between},
        AssetIdentityRedirect{between,    kept},
        AssetIdentityRedirect{  alive,    kept}
    };
    const expected<AssetRegistry> registry = AssetRegistry::merge( vector<AssetRegistryLayer>{ layer } );
    SW_REQUIRE_TRUE( registry.has_value() );
    SW_EXPECT_EQUAL( string( "common/kept.xml" ), registry->findPath( merged ).value_or( string{} ) );
    SW_EXPECT_EQUAL( string( "common/alive.xml" ), registry->findPath( alive ).value_or( string{} ) );

    AssetRegistry                        previous = AssetRegistry::install( *registry );
    const expected<AssetReferenceTarget> target   = AssetRegistry::readReferenceText( AssetRegistry::makeReferenceText( merged ) );
    (void)AssetRegistry::install( std::move( previous ) );
    SW_REQUIRE_TRUE( target.has_value() );
    SW_EXPECT_TRUE( target->_guid == kept );

    AssetRegistryLayer loop = Util::makeLayer( "loop", {} );
    loop._listRedirect      = {
        AssetIdentityRedirect{ merged, between},
        AssetIdentityRedirect{between,  merged}
    };
    SW_EXPECT_FALSE( AssetRegistry::merge( vector<AssetRegistryLayer>{ loop } ).has_value() );
}
