// `.heightfield` · `.layerweights` 바이트는 신뢰 경계다 — 어느 길이로 잘려도 거절하고 내용을 남기지 않는다(nogpu).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/LayerWeightsAsset.h"

using namespace sw;

/** @brief [TerrainAssetBytesTest] 하이트필드는 왕복하고, 어느 길이로 잘려도 · 해상도를 꾸며도 · 뒤에 남아도 거절한다 */
SW_TEST_CASE( TerrainAssetBytesTest, HeightfieldRefusesTruncationAndForgedResolution )
{
    SW_EXPECT_LOG( "HeightfieldAsset.NotHeightfieldMagicSWHF" );
    SW_EXPECT_LOG( "HeightfieldAsset.BytesResolutionNeeds" );
    SW_EXPECT_LOG( "HeightfieldAsset.ResolutionExpected" );

    HeightfieldAsset source;
    source._resolution = 2;
    source._listHeight = vector<uint16>{ 1, 2, 3, 65535 };
    vector<uint8> bytes;
    source.saveToMemory( bytes );

    HeightfieldAsset read;
    SW_REQUIRE_TRUE( read.loadFromMemory( bytes, "good" ) );
    SW_EXPECT_EQUAL( 2u, read._resolution );
    SW_EXPECT_TRUE( read._listHeight == source._listHeight );
    SW_EXPECT_TRUE( read._listHoleCell.empty() );

    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        const vector<uint8> truncated( bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>( length ) );
        HeightfieldAsset    asset;
        SW_EXPECT_FALSE( asset.loadFromMemory( truncated, "truncated" ) );
        SW_EXPECT_TRUE( asset._listHeight.empty() );
    }

    vector<uint8>    forged = bytes;
    constexpr uint32 kHuge  = 0xFFFFFFFFu;
    Memory::copy( forged.data() + 8, &kHuge, sizeof( kHuge ) );
    SW_EXPECT_FALSE( read.loadFromMemory( forged, "forged" ) );

    vector<uint8> longer = bytes;
    longer.push_back( 0 );
    SW_EXPECT_FALSE( read.loadFromMemory( longer, "longer" ) );
}

/** @brief [TerrainAssetBytesTest] 레이어 무게 맵은 왕복하고, 어느 길이로 잘려도 · 크기를 꾸며도 거절한다 */
SW_TEST_CASE( TerrainAssetBytesTest, LayerWeightsRefuseTruncationAndForgedSize )
{
    SW_EXPECT_LOG( "LayerWeightsAsset.NotLayerWeightMap" );
    SW_EXPECT_LOG( "LayerWeightsAsset.BytesXNeeds" );
    SW_EXPECT_LOG( "LayerWeightsAsset.XLayersExpectedPer" );

    LayerWeightsAsset source;
    source._width  = 1;
    source._height = 1;
    source._bytes.assign( LayerWeightsAsset::kLayerCount, uint8{ 7 } );
    vector<uint8> bytes;
    source.saveToMemory( bytes );

    LayerWeightsAsset read;
    SW_REQUIRE_TRUE( read.loadFromMemory( bytes, "good" ) );
    SW_EXPECT_TRUE( read._bytes == source._bytes );

    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        const vector<uint8> truncated( bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>( length ) );
        LayerWeightsAsset   asset;
        SW_EXPECT_FALSE( asset.loadFromMemory( truncated, "truncated" ) );
        SW_EXPECT_TRUE( asset._bytes.empty() );
    }

    vector<uint8>    forged = bytes;
    constexpr uint32 kHuge  = 0xFFFFFFFFu;
    Memory::copy( forged.data() + 8, &kHuge, sizeof( kHuge ) );
    SW_EXPECT_FALSE( read.loadFromMemory( forged, "forged" ) );
}
