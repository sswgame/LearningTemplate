// 콘텐츠 브라우저 저장 검색 · 컬렉션 — 줄 텍스트 왕복, 범위(로컬 · 공유) 분리, 같은 이름 덮기, 중복 에셋 거절.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserCollectionStore.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [ContentBrowserCollectionStoreTest] 저장 검색과 컬렉션(빈 것 포함)이 줄 텍스트로 돌아오고, 범위마다 따로 읽힌다
 */
SW_TEST_CASE( ContentBrowserCollectionStoreTest, RoundTripKeepsScopes )
{
    ContentBrowserCollectionStore source;
    SW_EXPECT_TRUE( source.saveSearch( ContentBrowserSavedSearch{ "Crates", "Mesh", "crate", false } ) );
    SW_EXPECT_TRUE( source.saveSearch( ContentBrowserSavedSearch{ "Team Materials", "Material", "stone", true } ) );
    SW_EXPECT_TRUE( source.createCollection( "Level Kit", false ) );
    SW_EXPECT_TRUE( source.addToCollection( "Level Kit", false, "game/playground/models/crate_medium.mesh" ) );
    SW_EXPECT_FALSE( source.addToCollection( "Level Kit", false, "game/playground/models/crate_medium.mesh" ) ); // 같은 에셋은 한 번
    SW_EXPECT_TRUE( source.createCollection( "Empty Shared", true ) );
    SW_EXPECT_FALSE( source.createCollection( "Empty Shared", true ) ); // 같은 범위 · 같은 이름

    ContentBrowserCollectionStore loaded;
    loaded.fromText( source.toText( false ), false );
    loaded.fromText( source.toText( true ), true );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), loaded.getSavedSearches().size() );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), loaded.getCollections().size() );
    const ContentBrowserCollection* pKit = loaded.findCollection( "Level Kit", false );
    SW_REQUIRE_NOT_NULL( pKit );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), pKit->getAssetIDs().size() );
    SW_EXPECT_EQUAL( string( "game/playground/models/crate_medium.mesh" ), pKit->getAssetIDs()[0] );
    const ContentBrowserCollection* pShared = loaded.findCollection( "Empty Shared", true );
    SW_REQUIRE_NOT_NULL( pShared );
    SW_EXPECT_TRUE( pShared->getAssetIDs().empty() );
    SW_EXPECT_TRUE( loaded.findCollection( "Empty Shared", false ) == nullptr );

    // 한 범위를 다시 읽으면 그 범위만 바뀐다
    loaded.fromText( "", true );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), loaded.getSavedSearches().size() );
    SW_EXPECT_TRUE( loaded.findCollection( "Level Kit", false ) != nullptr );
}

/**
 * @brief [ContentBrowserCollectionStoreTest] 같은 이름으로 다시 저장하면 바꾸고, 지우면 빠진다. 칸 안의 탭은 공백이 되어 줄 형식이 깨지지 않는다
 */
SW_TEST_CASE( ContentBrowserCollectionStoreTest, OverwriteRemoveAndSanitize )
{
    ContentBrowserCollectionStore collections;
    SW_EXPECT_TRUE( collections.saveSearch( ContentBrowserSavedSearch{ "Rocks", "", "rock", false } ) );
    SW_EXPECT_TRUE( collections.saveSearch( ContentBrowserSavedSearch{ "Rocks", "Mesh", "rock\tbig", false } ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), collections.getSavedSearches().size() );
    SW_EXPECT_EQUAL( string( "Mesh" ), collections.getSavedSearches()[0]._typeFilterLabel );

    ContentBrowserCollectionStore loaded;
    loaded.fromText( collections.toText( false ), false );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), loaded.getSavedSearches().size() );
    SW_EXPECT_EQUAL( string( "rock big" ), loaded.getSavedSearches()[0]._searchText );

    SW_EXPECT_TRUE( collections.removeSearch( "Rocks", false ) );
    SW_EXPECT_FALSE( collections.removeSearch( "Rocks", false ) );
    SW_EXPECT_FALSE( collections.saveSearch( ContentBrowserSavedSearch{ "", "", "x", false } ) );
}

/**
 * @brief [ContentBrowserCollectionStoreTest] 뺀 에셋은 다시 더할 수 있고(목록과 색인이 함께 바뀐다), 파일에 겹쳐 적힌 줄은 한 번만 남는다
 */
SW_TEST_CASE( ContentBrowserCollectionStoreTest, RemovedAssetCanBeAddedAgain )
{
    ContentBrowserCollectionStore collections;
    collections.fromText( "collection\tKit\t\ncollection\tKit\ta.mesh\ncollection\tKit\tb.mesh\ncollection\tKit\ta.mesh\n", false );
    const ContentBrowserCollection* pKit = collections.findCollection( "Kit", false );
    SW_REQUIRE_NOT_NULL( pKit );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), pKit->getAssetIDs().size() );

    SW_EXPECT_TRUE( collections.removeFromCollection( "Kit", false, "a.mesh" ) );
    SW_EXPECT_FALSE( collections.removeFromCollection( "Kit", false, "a.mesh" ) );
    SW_EXPECT_TRUE( collections.addToCollection( "Kit", false, "a.mesh" ) );
    SW_EXPECT_FALSE( collections.addToCollection( "Kit", false, "b.mesh" ) );
    pKit = collections.findCollection( "Kit", false );
    SW_REQUIRE_NOT_NULL( pKit );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), pKit->getAssetIDs().size() );
    SW_EXPECT_EQUAL( string( "b.mesh" ), pKit->getAssetIDs()[0] );
    SW_EXPECT_EQUAL( string( "a.mesh" ), pKit->getAssetIDs()[1] );
}
