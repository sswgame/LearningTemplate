// `.heightfield` · `.layerweights` 바이트는 신뢰 경계다 — 어느 길이로 잘려도 거절하고 내용을 남기지 않는다(nogpu).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/LayerWeightsAsset.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"

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

/**
 * @brief [TerrainAssetBytesTest] 편집기 저장 왕복: 읽은 높이장 · 구멍 · 스플랫을 지형에 풀고 되돌려 쓰면 같은 바이트다(두 번 돌아도)
 * @details 지형 칠하기 도구의 저장(`TerrainBrushTool` 의 Save Terrain)은 지형의 월드 높이를 원시 높이로 되돌리고(`makeHeightfieldAsset`) 스플랫 바이트를 그대로 쓴다.
 *          그 길은 GPU · 씬 없이 이 둘이라 시나리오가 아니라 여기서 본다. 높이를 풀고 되돌리는 양자화가 한 칸이라도 밀리면 고치지 않은 지형을 저장할 때마다 땅이 움직인다.
 */
SW_TEST_CASE( TerrainAssetBytesTest, EditorSaveRoundTripKeepsBytes )
{
    constexpr float32 kHeightMin = -10.0f;
    constexpr float32 kHeightMax = 40.0f;
    const float3      origin{ -128.0f, 2.5f, -128.0f };
    const float2      size{ 256.0f, 256.0f };

    HeightfieldAsset source;
    source._resolution   = 3;
    source._listHeight   = vector<uint16>{ 0, 1, 2, 12345, 32767, 32768, 54321, 65534, 65535 };
    source._listHoleCell = vector<uint8>{ 0, 1, 0, 0 };
    vector<uint8> sourceBytes;
    source.saveToMemory( sourceBytes );

    LayerWeightsAsset splat;
    splat._width  = 2;
    splat._height = 1;
    splat._bytes.assign( static_cast<size_t>( splat._width ) * splat._height * LayerWeightsAsset::kLayerCount, uint8{ 0 } );
    splat._bytes[0]                                  = 255;
    splat._bytes[LayerWeightsAsset::kLayerCount]     = 128;
    splat._bytes[LayerWeightsAsset::kLayerCount + 3] = 127;
    vector<uint8> splatBytes;
    splat.saveToMemory( splatBytes );

    vector<uint8> currentBytes = sourceBytes;
    for ( uint32 passIndex = 0; passIndex < 2; ++passIndex )
    {
        HeightfieldAsset read;
        SW_REQUIRE_TRUE( read.loadFromMemory( currentBytes, "roundtrip" ) );
        TerrainHeightfield heightfield;
        SW_REQUIRE_TRUE( heightfield.initialize( read, origin, size, kHeightMin, kHeightMax ) );
        heightfield.setSplat( splat._width, splat._height, splat._bytes );

        HeightfieldAsset saved;
        heightfield.makeHeightfieldAsset( kHeightMin, kHeightMax, saved );
        vector<uint8> savedBytes;
        saved.saveToMemory( savedBytes );
        SW_EXPECT_TRUE( savedBytes == sourceBytes );

        LayerWeightsAsset savedSplat;
        savedSplat._width  = heightfield.getSplatWidth();
        savedSplat._height = heightfield.getSplatHeight();
        savedSplat._bytes  = heightfield.getSplatBytes();
        vector<uint8> savedSplatBytes;
        savedSplat.saveToMemory( savedSplatBytes );
        SW_EXPECT_TRUE( savedSplatBytes == splatBytes );
        currentBytes = savedBytes;
    }
}
