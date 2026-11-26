#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Asset/AssetPackIndex.h"

using namespace sw;

SW_TEST_CASE( AssetPackIndexTest, TextIsSortedByGUIDAndReadsBack )
{
    const AssetGUID low  = *AssetGUID::tryParse( "00000000000000000000000000000001" );
    const AssetGUID high = *AssetGUID::tryParse( "ff000000000000000000000000000000" );
    AssetPackIndex  index;
    index._listEntry.push_back( AssetPackIndexEntry{ high, "prefabs/b.prefab.xml" } );
    index._listEntry.push_back( AssetPackIndexEntry{ low, "textures_raw/a.png" } );
    index._listRedirect.push_back( AssetIdentityRedirect{ AssetGUID::create(), low } );
    const expected<string> text = index.makeText();
    SW_REQUIRE_TRUE( text.has_value() );
    SW_EXPECT_TRUE( text->find( "00000000000000000000000000000001" ) < text->find( "ff000000000000000000000000000000" ) ); // 같은 입력은 같은 바이트

    const expected<AssetPackIndex> read = AssetPackIndex::parseText( *text, "test" );
    SW_REQUIRE_TRUE( read.has_value() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, read->_listEntry.size() );
    SW_EXPECT_TRUE( read->_listEntry[0]._guid == low );
    SW_EXPECT_EQUAL( string( "textures_raw/a.png" ), read->_listEntry[0]._path );
    SW_EXPECT_EQUAL( string( "prefabs/b.prefab.xml" ), read->_listEntry[1]._path );
    SW_REQUIRE_EQUAL( size_t{ 1 }, read->_listRedirect.size() ); // 정체성 대체 줄도 색인이 나른다
    SW_EXPECT_TRUE( read->_listRedirect[0]._from == index._listRedirect[0]._from );
    SW_EXPECT_TRUE( read->_listRedirect[0]._to == low );
}

SW_TEST_CASE( AssetPackIndexTest, RefusesRepeatedLinesAndOtherShapes )
{
    const AssetGUID guid = AssetGUID::create();
    AssetPackIndex  repeatedGUID;
    repeatedGUID._listEntry.push_back( AssetPackIndexEntry{ guid, "a.xml" } );
    repeatedGUID._listEntry.push_back( AssetPackIndexEntry{ guid, "b.xml" } );
    SW_EXPECT_FALSE( repeatedGUID.makeText().has_value() );

    AssetPackIndex repeatedPath;
    repeatedPath._listEntry.push_back( AssetPackIndexEntry{ AssetGUID::create(), "a.xml" } );
    repeatedPath._listEntry.push_back( AssetPackIndexEntry{ AssetGUID::create(), "A.xml" } );
    SW_EXPECT_FALSE( repeatedPath.makeText().has_value() );

    SW_EXPECT_FALSE( AssetPackIndex::parseText( "<AssetIndex formatVersion=\"1\"/>", "newer" ).has_value() );
    SW_EXPECT_FALSE( AssetPackIndex::parseText( "<AssetIndex formatVersion=\"0\"><Asset guid=\"nope\" path=\"a.xml\"/></AssetIndex>", "bad guid" ).has_value() );
    SW_EXPECT_FALSE( AssetPackIndex::parseText( "<Other formatVersion=\"0\"/>", "other root" ).has_value() );
}
