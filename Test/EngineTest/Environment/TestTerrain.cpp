#include "pch.h"

#include "Core/Common/BitUtil.h"
#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/LayerWeightsData.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Environment/Terrain/TerrainMeshBuilder.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Image/ImageFileWriter.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Format/Archive.h"

#include "EngineTest/HostTargetTestUtil.h"

#include "TestFramework/TestFramework.h"

// 지형 — 높이장 형식 · 해석 곡면 대비 높이 · 노멀 · 구멍 · 스플랫 정규화 · 청크 LOD 의 틈 없음 · 컴포넌트 LOD 갱신.

using namespace sw;

namespace
{
    struct TerrainTestUtil
    {
        static constexpr float32 kHeightMin = -10.0f;
        static constexpr float32 kHeightMax = 10.0f;

        /** @brief 해석 곡면 h(x, z) 를 해상도 @p resolution · 칸 1 m 로 베이크합니다. */
        template <typename TSurface>
        static HeightfieldData makeData( uint32 resolution, const TSurface& surface )
        {
            HeightfieldData data;
            data._resolution = resolution;
            data._listHeight.resize( static_cast<size_t>( resolution ) * resolution );
            for ( uint32 sampleZ = 0; sampleZ < resolution; ++sampleZ )
            {
                for ( uint32 sampleX = 0; sampleX < resolution; ++sampleX )
                {
                    const float32 height                                                    = surface( static_cast<float32>( sampleX ), static_cast<float32>( sampleZ ) );
                    const float32 encoded                                                   = ( height - kHeightMin ) / ( kHeightMax - kHeightMin ) * 65535.0f;
                    data._listHeight[static_cast<size_t>( sampleZ ) * resolution + sampleX] = static_cast<uint16>( MathUtil::clamp( MathUtil::round( encoded ), 0.0f, 65535.0f ) );
                }
            }
            return data;
        }

        static float32 wave( float32 x, float32 z ) { return 5.0f * MathUtil::sin( x * 0.1f ) * MathUtil::cos( z * 0.1f ); }

        /** @brief @p size × @p size 의 가중치 맵을 모두 레이어 @p layer 로 채워 만듭니다. */
        static LayerWeightsData makeUniformWeights( uint32 size, uint32 layer )
        {
            LayerWeightsData data;
            data._width  = size;
            data._height = size;
            data._bytes.assign( static_cast<size_t>( size ) * size * LayerWeightsData::kLayerCount, 0u );
            for ( size_t texel = 0; texel < static_cast<size_t>( size ) * size; ++texel )
            {
                data._bytes[texel * LayerWeightsData::kLayerCount + layer] = 255u;
            }
            return data;
        }

        /** @brief 텍셀마다 레이어 여덟 바이트인 가중치에서 평면 @p plane(0 = 레이어 0..3, 1 = 4..7)의 RGBA8 을 뽑습니다 - GPU 스플랫 텍스처 한 장의 내용입니다. */
        static vector<uint8> planeBytes( const vector<uint8>& layerBytes, uint32 plane )
        {
            vector<uint8> listRgba;
            for ( size_t texel = 0; texel * LayerWeightsData::kLayerCount < layerBytes.size(); ++texel )
            {
                for ( uint32 channel = 0; channel < 4; ++channel )
                {
                    listRgba.push_back( layerBytes[texel * LayerWeightsData::kLayerCount + plane * 4 + channel] );
                }
            }
            return listRgba;
        }

        /** @brief 높이장 하나와 가중치 맵 하나를 쓰는 32 m 지형을 @p manager 에 만듭니다(읽기 `reloadTerrain` 은 부르는 쪽이 합니다). */
        static TerrainComponent* makeTerrain( GameObjectManager& manager, const utf8* pName, const string& heightfieldPath, const string& weightsPath )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
            if ( pTerrain == nullptr )
                return nullptr;
            pTerrain->setHeightfieldPath( heightfieldPath );
            pTerrain->setLayerWeightsPath( weightsPath );
            pTerrain->setSize( float2{ 32.0f, 32.0f } );
            pTerrain->setHeightRange( kHeightMin, kHeightMax );
            pTerrain->setChunkCells( 16 );
            pTerrain->setMaterialPath( "" );
            return pTerrain;
        }

        static TerrainHeightfield makeWaveField( uint32 resolution )
        {
            TerrainHeightfield field;
            const float32      extent = static_cast<float32>( resolution - 1 );
            (void)field.initialize( makeData( resolution, &TerrainTestUtil::wave ), float3{}, float2{ extent, extent }, kHeightMin, kHeightMax );
            return field;
        }

        /** @brief 청크 정점 가운데 월드 x == @p edgeX 인 것의 (x, y, z) 비트 집합입니다(GPU 와 같이 로컬 + 이동으로 더한다). */
        static void collectEdgeVertices( const vector<RHIVertex>& listVertex, const float3& translation, float32 edgeX, vector<uint64>& outListKey )
        {
            outListKey.clear();
            for ( const RHIVertex& vertex : listVertex )
            {
                const float32 worldX = vertex._arrPosition[0] + translation._x;
                if ( worldX != edgeX )
                    continue;
                const float32 worldY = vertex._arrPosition[1] + translation._y;
                const float32 worldZ = vertex._arrPosition[2] + translation._z;
                const uint32  bitsY  = BitUtil::floatToBits( worldY );
                const uint32  bitsZ  = BitUtil::floatToBits( worldZ );
                outListKey.push_back( ( static_cast<uint64>( bitsZ ) << 32 ) | bitsY );
            }
            VectorUtil::sortUnique( outListKey );
        }

        /** @brief 삼각형 목록이 xz 로 덮는 넓이의 합입니다(겹치거나 빠지면 청크 넓이와 다르다). */
        static float32 computeCoveredArea( const vector<RHIVertex>& listVertex )
        {
            float32 area{ 0.0f };
            for ( size_t index = 0; index + 2 < listVertex.size(); index += 3 )
            {
                const RHIVertex& a     = listVertex[index];
                const RHIVertex& b     = listVertex[index + 1];
                const RHIVertex& c     = listVertex[index + 2];
                const float32    cross = ( b._arrPosition[0] - a._arrPosition[0] ) * ( c._arrPosition[2] - a._arrPosition[2] ) -
                                      ( b._arrPosition[2] - a._arrPosition[2] ) * ( c._arrPosition[0] - a._arrPosition[0] );
                area -= 0.5f * cross; // 위에서 앞면인 감김은 (x 오른쪽 · z 위) 평면에서 시계 방향 — 음의 외적이다
            }
            return area;
        }
    };
} // namespace

/**
 * @brief [TerrainTest] 높이장 형식은 쓰고 읽으면 같다(구멍 포함) · 매직 · 판 · 크기가 틀리면 읽지 않는다
 */
SW_TEST_CASE( TerrainTest, HeightfieldFormatRoundTrips )
{
    HeightfieldData data = TerrainTestUtil::makeData( 9, &TerrainTestUtil::wave );
    data._listHoleCell.assign( 8 * 8, 0u );
    data._listHoleCell[3 * 8 + 5] = 1u;
    vector<uint8> bytes;
    data.saveToMemory( bytes );
    HeightfieldData loaded;
    SW_ASSERT_TRUE( loaded.loadFromMemory( bytes, "roundtrip" ) );
    SW_EXPECT_EQUAL( 9u, loaded._resolution );
    SW_EXPECT_TRUE( loaded._listHeight == data._listHeight );
    SW_EXPECT_TRUE( loaded.isHoleCell( 5, 3 ) );
    SW_EXPECT_FALSE( loaded.isHoleCell( 3, 5 ) );

    SW_TEST_DEFENSIVE_SCOPE( "malformed heightfield bytes are rejected" );
    vector<uint8> truncated( bytes.begin(), bytes.end() - 1 );
    SW_EXPECT_FALSE( loaded.loadFromMemory( truncated, "truncated" ) );
    vector<uint8> badMagic = bytes;
    badMagic[0]            = 'X';
    SW_EXPECT_FALSE( loaded.loadFromMemory( badMagic, "magic" ) );
}

/**
 * @brief [TerrainTest] 평면은 칸 안 어디서나 정확히(양자화 오차 안에서) 나오고, 사인 곡면은 칸 크기의 2 차 오차 안이다 · 지형 밖은 없다
 */
SW_TEST_CASE( TerrainTest, HeightSamplingMatchesAnalyticSurfaces )
{
    auto plane = []( float32 x, float32 z )
    { return 0.3f * x - 0.2f * z + 1.0f; };
    TerrainHeightfield planeField;
    SW_ASSERT_TRUE( planeField.initialize( TerrainTestUtil::makeData( 33, plane ), float3{}, float2{ 32.0f, 32.0f }, TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax ) );
    const float32 quantization = ( TerrainTestUtil::kHeightMax - TerrainTestUtil::kHeightMin ) / 65535.0f;
    float32       planeError{ 0.0f };
    for ( uint32 probe = 0; probe < 400; ++probe )
    {
        const float32 x = 0.05f + static_cast<float32>( probe % 20 ) * 1.57f;
        const float32 z = 0.11f + static_cast<float32>( probe / 20 ) * 1.53f;
        float32       height{ 0.0f };
        SW_ASSERT_TRUE( planeField.findHeightAt( x, z, height ) );
        planeError = MathUtil::max( planeError, MathUtil::abs( height - plane( x, z ) ) );
    }
    SW_EXPECT_TRUE( planeError <= quantization * 2.0f );

    const TerrainHeightfield waveField = TerrainTestUtil::makeWaveField( 65 );
    float32                  waveError{ 0.0f };
    for ( uint32 probe = 0; probe < 400; ++probe )
    {
        const float32 x = 0.37f + static_cast<float32>( probe % 20 ) * 3.1f;
        const float32 z = 0.73f + static_cast<float32>( probe / 20 ) * 3.05f;
        float32       height{ 0.0f };
        SW_ASSERT_TRUE( waveField.findHeightAt( x, z, height ) );
        waveError = MathUtil::max( waveError, MathUtil::abs( height - TerrainTestUtil::wave( x, z ) ) );
    }
    // 칸 1 m, 곡률 ≤ 5 × 0.01 · 2 — 삼각형 보간의 오차 상한은 약 0.02 m 다.
    SW_EXPECT_TRUE( waveError < 0.02f );
    float32 outside{ 0.0f };
    SW_EXPECT_FALSE( waveField.findHeightAt( -0.5f, 3.0f, outside ) );
    SW_EXPECT_FALSE( waveField.findHeightAt( 3.0f, 64.5f, outside ) );
}

/**
 * @brief [TerrainTest] 높이 질의는 메시와 같은 대각선 (0,0)–(1,1) 로 나눈 삼각형을 따른다 — 솟은 샘플 하나 옆에서 두 삼각형의 값이 다르다
 * @details 평면 · 매끈한 곡면은 어느 대각선으로 나눠도 값이 거의 같아 이 규칙을 보지 못한다. 샘플 (1, 0) 만 솟은 칸에서 (0.75, 0.25) 는
 *          x 쪽 삼각형(00 · 11 · 10)이라 솟은 높이의 절반, (0.25, 0.75) 는 z 쪽 삼각형(00 · 01 · 11)이라 0 이다.
 */
SW_TEST_CASE( TerrainTest, HeightQueryFollowsTheMeshTriangles )
{
    HeightfieldData data;
    data._resolution = 3;
    data._listHeight.assign( 9, 0u );
    data._listHeight[1] = 65535u; // 샘플 (1, 0)
    TerrainHeightfield field;
    SW_ASSERT_TRUE( field.initialize( data, float3{}, float2{ 2.0f, 2.0f }, 0.0f, 8.0f ) );
    float32 height{ 0.0f };
    SW_ASSERT_TRUE( field.findHeightAt( 0.75f, 0.25f, height ) );
    SW_EXPECT_NEAR_EQUAL( 4.0f, height, 1.0e-5f );
    SW_ASSERT_TRUE( field.findHeightAt( 0.25f, 0.75f, height ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, height, 1.0e-5f );
}

/**
 * @brief [TerrainTest] 노멀은 해석 노멀(−∂h/∂x, 1, −∂h/∂z) 과 맞는다 — 샘플과 칸 안 질의 모두
 */
SW_TEST_CASE( TerrainTest, NormalsMatchAnalyticSurface )
{
    const TerrainHeightfield field = TerrainTestUtil::makeWaveField( 65 );
    float32                  worstDot{ 1.0f };
    for ( uint32 probe = 0; probe < 200; ++probe )
    {
        const float32 x = 2.3f + static_cast<float32>( probe % 20 ) * 3.0f;
        const float32 z = 1.9f + static_cast<float32>( probe / 20 ) * 6.0f;
        float3        normal{};
        SW_ASSERT_TRUE( field.findNormalAt( x, z, normal ) );
        const float32 slopeX   = 0.5f * MathUtil::cos( x * 0.1f ) * MathUtil::cos( z * 0.1f );
        const float32 slopeZ   = -0.5f * MathUtil::sin( x * 0.1f ) * MathUtil::sin( z * 0.1f );
        const float3  expected = float3{ -slopeX, 1.0f, -slopeZ }.normalize();
        worstDot               = MathUtil::min( worstDot, normal.dot( expected ) );
    }
    SW_EXPECT_TRUE( worstDot > 0.9995f );
    const float3 sampleNormal = field.computeSampleNormal( 20, 30 );
    const float3 expected     = float3{ -0.5f * MathUtil::cos( 2.0f ) * MathUtil::cos( 3.0f ), 1.0f, 0.5f * MathUtil::sin( 2.0f ) * MathUtil::sin( 3.0f ) }.normalize();
    SW_EXPECT_TRUE( sampleNormal.dot( expected ) > 0.9995f );
}

/**
 * @brief [TerrainTest] 구멍 칸은 높이 질의가 실패하고 메시에 삼각형이 없다 · 이웃 칸은 그대로다
 */
SW_TEST_CASE( TerrainTest, HolesAreQueryableAndCutFromTheMesh )
{
    HeightfieldData data = TerrainTestUtil::makeData( 17, &TerrainTestUtil::wave );
    data._listHoleCell.assign( 16 * 16, 0u );
    data._listHoleCell[4 * 16 + 6] = 1u; // 칸 (6, 4)
    TerrainHeightfield field;
    SW_ASSERT_TRUE( field.initialize( data, float3{}, float2{ 16.0f, 16.0f }, TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax ) );
    float32 height{ 0.0f };
    SW_EXPECT_FALSE( field.findHeightAt( 6.5f, 4.5f, height ) );
    SW_EXPECT_TRUE( field.isHoleAt( 6.5f, 4.5f ) );
    SW_EXPECT_TRUE( field.findHeightAt( 7.5f, 4.5f, height ) );

    TerrainChunkLayout layout;
    SW_ASSERT_TRUE( TerrainMeshBuilder::makeLayout( 17, 16, layout ) );
    const uint32      arrNeighbor[4] = { 0, 0, 0, 0 };
    vector<RHIVertex> listVertex;
    TerrainMeshBuilder::makeChunkVertices( field, layout, 0, 0, 0, arrNeighbor, listVertex );
    SW_EXPECT_NEAR_EQUAL( 16.0f * 16.0f - 1.0f, TerrainTestUtil::computeCoveredArea( listVertex ), 1.0e-3f );
}

/**
 * @brief [TerrainTest] 스플랫 가중치는 합이 1 로 나뉜다(셰이더와 같은 식) · 모두 0 이면 레이어 0 이다 · 텍셀 중심이 지형 가장자리다
 */
SW_TEST_CASE( TerrainTest, SplatWeightsNormalize )
{
    const SurfaceLayerWeights zero = SurfaceLayerWeights{
        float4{0.0f, 0.0f, 0.0f, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 0.0f}
    }
                                         .computeNormalized();
    SW_EXPECT_NEAR_EQUAL( 1.0f, zero._low._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, zero._low._y + zero._low._z + zero._low._w + zero._high._x + zero._high._y + zero._high._z + zero._high._w, 1.0e-6f );

    TerrainHeightfield field = TerrainTestUtil::makeWaveField( 17 );
    // 3 × 2 스플랫: 아무 값(합이 1 이 아님) · 모두 0 칸 하나.
    const vector<uint8> layerBytes = { 200, 40, 0, 0, 0, 0, 0, 0, 10, 10, 10, 10, 10, 10, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0, 90, 0, 30, 120, 60, 0, 0, 0, 7, 3, 1, 250, 0, 0, 0, 5 };
    field.setSplat( 3, 2, layerBytes );
    float32 worstSum{ 0.0f };
    for ( uint32 probe = 0; probe < 64; ++probe )
    {
        const SurfaceLayerWeights weight = field.computeLayerWeightsAt( static_cast<float32>( probe % 8 ) * 2.1f, static_cast<float32>( probe / 8 ) * 2.05f );
        const float32             sum    = weight.getSum();
        worstSum                         = MathUtil::max( worstSum, MathUtil::abs( sum - 1.0f ) );
    }
    SW_EXPECT_TRUE( worstSum < 1.0e-5f );
    // 모서리 (0, 0) 는 첫 텍셀 그대로(200, 40) → (0.833, 0.167).
    const SurfaceLayerWeights corner = field.computeLayerWeightsAtUv( 0.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 200.0f / 240.0f, corner._low._x, 1.0e-5f );
    // 셋째 텍셀(u = 1, v = 0)은 모두 0 → 레이어 0.
    const SurfaceLayerWeights empty = field.computeLayerWeightsAtUv( 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, empty._low._x, 1.0e-5f );
}

/**
 * @brief [TerrainTest] LOD 가 다른 이웃 청크의 맞닿은 변은 정점 집합이 비트까지 같다(T 접합 · 틈 없음) · 접어도 청크 넓이는 그대로다
 * @details LOD 0..4 의 모든 쌍을 x 로 이웃한 두 청크에 건다. 고운 쪽이 변을 접지 않으면 그 변에 거친 쪽에 없는 정점이 생겨 집합이 갈린다.
 */
SW_TEST_CASE( TerrainTest, LODEdgesAreCrackFree )
{
    const TerrainHeightfield field = TerrainTestUtil::makeWaveField( 65 );
    TerrainChunkLayout       layout;
    SW_ASSERT_TRUE( TerrainMeshBuilder::makeLayout( 65, 16, layout ) );
    SW_EXPECT_EQUAL( 4u, layout._chunkCountX );
    SW_EXPECT_EQUAL( 4u, layout._maxLOD );

    const float32     edgeX = 32.0f; // 청크 1 과 2 사이
    uint32            mismatchCount{ 0 };
    uint32            areaErrorCount{ 0 };
    vector<RHIVertex> listLeft;
    vector<RHIVertex> listRight;
    vector<uint64>    listLeftKey;
    vector<uint64>    listRightKey;
    for ( uint32 leftLOD = 0; leftLOD <= layout._maxLOD; ++leftLOD )
    {
        for ( uint32 rightLOD = 0; rightLOD <= layout._maxLOD; ++rightLOD )
        {
            const uint32 arrLeftNeighbor[4]  = { leftLOD, rightLOD, leftLOD, leftLOD };
            const uint32 arrRightNeighbor[4] = { leftLOD, rightLOD, rightLOD, rightLOD };
            TerrainMeshBuilder::makeChunkVertices( field, layout, 1, 1, leftLOD, arrLeftNeighbor, listLeft );
            TerrainMeshBuilder::makeChunkVertices( field, layout, 2, 1, rightLOD, arrRightNeighbor, listRight );
            TerrainTestUtil::collectEdgeVertices( listLeft, TerrainMeshBuilder::computeChunkTranslation( field, layout, 1, 1 ), edgeX, listLeftKey );
            TerrainTestUtil::collectEdgeVertices( listRight, TerrainMeshBuilder::computeChunkTranslation( field, layout, 2, 1 ), edgeX, listRightKey );
            const uint32 coarseStep = 1u << MathUtil::max( leftLOD, rightLOD );
            if ( listLeftKey != listRightKey || listLeftKey.size() != 16 / coarseStep + 1 )
                ++mismatchCount;
            if ( MathUtil::abs( TerrainTestUtil::computeCoveredArea( listLeft ) - 256.0f ) > 1.0e-2f ||
                 MathUtil::abs( TerrainTestUtil::computeCoveredArea( listRight ) - 256.0f ) > 1.0e-2f )
                ++areaErrorCount;
        }
    }
    SW_EXPECT_EQUAL( 0u, mismatchCount );
    SW_EXPECT_EQUAL( 0u, areaErrorCount );
}

/**
 * @brief [TerrainTest] LOD 는 거리의 두 배마다 하나씩 오르고 상한에서 멈춘다 · 청크 배치는 2 의 거듭제곱 · 나눠떨어짐을 요구한다
 */
SW_TEST_CASE( TerrainTest, LODSelectionDoublesWithDistance )
{
    SW_EXPECT_EQUAL( 0u, TerrainMeshBuilder::selectLOD( 10.0f, 40.0f, 5 ) );
    SW_EXPECT_EQUAL( 1u, TerrainMeshBuilder::selectLOD( 40.0f, 40.0f, 5 ) );
    SW_EXPECT_EQUAL( 1u, TerrainMeshBuilder::selectLOD( 79.0f, 40.0f, 5 ) );
    SW_EXPECT_EQUAL( 2u, TerrainMeshBuilder::selectLOD( 80.0f, 40.0f, 5 ) );
    SW_EXPECT_EQUAL( 5u, TerrainMeshBuilder::selectLOD( 100000.0f, 40.0f, 5 ) );
    TerrainChunkLayout layout;
    SW_EXPECT_FALSE( TerrainMeshBuilder::makeLayout( 65, 12, layout ) );
    SW_EXPECT_FALSE( TerrainMeshBuilder::makeLayout( 64, 16, layout ) );
    SW_EXPECT_TRUE( TerrainMeshBuilder::makeLayout( 257, 32, layout ) );
    SW_EXPECT_EQUAL( 8u, layout._chunkCountX );
    SW_EXPECT_EQUAL( 5u, layout._maxLOD );
}

/**
 * @brief [TerrainTest] 컴포넌트 — 에셋을 읽어 오너 자리에 펼치고, 먼 카메라에서는 청크를 거칠게 다시 만들며, 같은 자리면 다시 만들지 않는다
 */
SW_TEST_CASE( TerrainTest, ComponentLoadsAndUpdatesLODs )
{
    const string    path = test::makeTempPath( "wave.heightfield" );
    HeightfieldData data = TerrainTestUtil::makeData( 65, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( path ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Terrain" ) );
    SW_ASSERT_NOT_NULL( pObject );
    TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );
    pTerrain->setLocalPosition( float3{ 100.0f, 2.0f, -50.0f } );
    pTerrain->setHeightfieldPath( ::test::makeTempResourceID( path ) );
    pTerrain->setSize( float2{ 64.0f, 64.0f } );
    pTerrain->setHeightRange( TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax );
    pTerrain->setChunkCells( 16 );
    pTerrain->setLODDistance( 20.0f );
    pTerrain->setMaterialPath( "" ); // 그리지 않는 테스트 — 머티리얼을 잡지 않는다
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    SW_EXPECT_EQUAL( 16u, pTerrain->getChunkLayout().getChunkCount() );

    float32 height{ 0.0f };
    SW_ASSERT_TRUE( pTerrain->findHeightAt( 110.0f, -40.0f, height ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f + TerrainTestUtil::wave( 10.0f, 10.0f ), height, 1.0e-3f );
    SW_EXPECT_TRUE( TerrainComponent::findTerrainAt( manager, 110.0f, -40.0f ) == pTerrain );
    SW_EXPECT_TRUE( TerrainComponent::findTerrainAt( manager, 0.0f, 0.0f ) == nullptr );

    const uint32 nearVertexCount = pTerrain->getChunkVertexCount( 3, 3 );
    const uint32 rebuiltCount    = pTerrain->updateLODs( float3{ 100.0f, 2.0f, -50.0f } ); // 청크 (0, 0) 모서리
    SW_EXPECT_TRUE( rebuiltCount > 0 );
    SW_EXPECT_EQUAL( 0u, pTerrain->getChunkLOD( 0, 0 ) );
    SW_EXPECT_TRUE( pTerrain->getChunkLOD( 3, 3 ) >= 2u );
    SW_EXPECT_TRUE( pTerrain->getChunkVertexCount( 3, 3 ) < nearVertexCount );
    SW_EXPECT_EQUAL( 0u, pTerrain->updateLODs( float3{ 100.0f, 2.0f, -50.0f } ) );
}

/**
 * @brief [TerrainTest] 지형 컴포넌트나 그 오브젝트를 끄면 청크가 GPU 씬에서 빠지고, 켜면 돌아온다
 * @details 청크는 씬 컴포넌트 없는 인스턴스 배치라 빌더가 컴포넌트의 활성을 몰랐다 — 꺼도 지형이 그대로 그려졌다.
 */
SW_TEST_CASE( TerrainTest, ChunksLeaveTheGPUSceneWhenTheComponentOrOwnerIsOff )
{
    const string    path = test::makeTempPath( "toggle.heightfield" );
    HeightfieldData data = TerrainTestUtil::makeData( 33, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( path ) );

    Scene scene( "TerrainToggle" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    GameObject*       pObject  = scene.getObjectManager()->createGameObject( hashed_string( "Terrain" ) );
    TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );
    pTerrain->setHeightfieldPath( ::test::makeTempResourceID( path ) );
    pTerrain->setSize( float2{ 32.0f, 32.0f } );
    pTerrain->setHeightRange( TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax );
    pTerrain->setChunkCells( 16 );
    pTerrain->setMaterialPath( "" );
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    const uint32 chunkCount = pTerrain->getChunkLayout().getChunkCount();
    SW_ASSERT_TRUE( chunkCount > 0 );

    GPUSceneBuilder gpuScene;
    const float3    camPos{ 0.0f, 50.0f, 0.0f };
    gpuScene.populateFromScene( &scene, camPos );
    SW_ASSERT_EQUAL( chunkCount, static_cast<uint32>( gpuScene.getInstances().size() ) );

    pTerrain->setActive( false );
    gpuScene.populateFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    pTerrain->setActive( true );
    gpuScene.populateFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( chunkCount, static_cast<uint32>( gpuScene.getInstances().size() ) );

    pObject->setActive( false );
    gpuScene.populateFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    pObject->setActive( true );
    gpuScene.populateFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( chunkCount, static_cast<uint32>( gpuScene.getInstances().size() ) );
}

/**
 * @brief [TerrainTest] 높이 쓰기 — 사각형 밖은 그대로 · 질의와 세대가 따라가고 · 크기가 틀리면 거절 · 저장하면 다시 읽어도 같다
 */
SW_TEST_CASE( TerrainTest, WriteHeightsUpdatesQueriesAndSaves )
{
    const string    path = test::makeTempPath( "edit.heightfield" );
    HeightfieldData data = TerrainTestUtil::makeData( 65, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( path ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Terrain" ) );
    SW_ASSERT_NOT_NULL( pObject );
    TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );
    pTerrain->setHeightfieldPath( ::test::makeTempResourceID( path ) );
    pTerrain->setSize( float2{ 64.0f, 64.0f } );
    pTerrain->setHeightRange( TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax );
    pTerrain->setChunkCells( 16 );
    pTerrain->setMaterialPath( "" );
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );

    vector<float32>       listHeight = pTerrain->getHeightfield().getHeightSamples();
    const vector<float32> listBefore = listHeight;
    const uint32          generation = pTerrain->getHeightfieldGeneration();
    TerrainSampleRect     rect;
    rect.include( 16, 16 ); // 청크 경계 샘플 — 청크 네 개가 걸친다
    rect.include( 18, 17 );
    for ( uint32 sampleZ = rect._minZ; sampleZ <= rect._maxZ; ++sampleZ )
    {
        for ( uint32 sampleX = rect._minX; sampleX <= rect._maxX; ++sampleX )
        {
            listHeight[static_cast<size_t>( sampleZ ) * 65 + sampleX] = 7.0f;
        }
    }
    SW_ASSERT_TRUE( pTerrain->writeHeights( listHeight, rect ) );
    SW_EXPECT_EQUAL( generation + 1, pTerrain->getHeightfieldGeneration() );
    float32 height{ 0.0f };
    SW_ASSERT_TRUE( pTerrain->findHeightAt( 17.0f, 16.5f, height ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, height, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( listBefore[10 * 65 + 10], pTerrain->getHeightfield().getSampleHeight( 10, 10 ), 0.0f ); // 사각형 밖은 그대로
    SW_EXPECT_NEAR_EQUAL( listBefore[17 * 65 + 19], pTerrain->getHeightfield().getSampleHeight( 19, 17 ), 0.0f );

    SW_TEST_DEFENSIVE_SCOPE( "wrong size, empty or out-of-grid rectangles are rejected without changes" );
    vector<float32> listShort( 10, 0.0f );
    SW_EXPECT_FALSE( pTerrain->writeHeights( listShort, rect ) );
    SW_EXPECT_FALSE( pTerrain->writeHeights( listHeight, TerrainSampleRect{} ) );
    TerrainSampleRect outside;
    outside.include( 60, 60 );
    outside.include( 65, 65 );
    SW_EXPECT_FALSE( pTerrain->writeHeights( listHeight, outside ) );
    SW_EXPECT_EQUAL( generation + 1, pTerrain->getHeightfieldGeneration() );

    // 되돌리기 — 편집 전 사본을 같은 사각형으로 쓰면 원래 값이다.
    SW_ASSERT_TRUE( pTerrain->writeHeights( listBefore, rect ) );
    SW_EXPECT_NEAR_EQUAL( listBefore[16 * 65 + 17], pTerrain->getHeightfield().getSampleHeight( 17, 16 ), 0.0f );

    // 저장하면 다시 읽어도 양자화 오차 안에서 같다.
    SW_ASSERT_TRUE( pTerrain->writeHeights( listHeight, rect ) );
    const string savedPath = test::makeTempPath( "edit_saved.heightfield" );
    SW_ASSERT_TRUE( pTerrain->saveHeightfield( savedPath ) );
    HeightfieldData loaded;
    SW_ASSERT_TRUE( loaded.loadFromResource( ::test::makeTempResourceID( savedPath ) ) );
    SW_EXPECT_EQUAL( 65u, loaded._resolution );
    const float32 expectedRaw = ( 7.0f - TerrainTestUtil::kHeightMin ) / ( TerrainTestUtil::kHeightMax - TerrainTestUtil::kHeightMin ) * 65535.0f;
    SW_EXPECT_NEAR_EQUAL( expectedRaw, static_cast<float32>( loaded.getSample( 17, 16 ) ), 1.0f );
}

/**
 * @brief [TerrainTest] 저장하지 않은 높이 · 스플랫 편집은 상태 스냅샷(`writeState` → `restoreState`)으로 다시 읽은 지형에 돌아간다 — 모듈 리로드 · 백엔드 교체 경로
 * @details 변이: `writeState` 가 높이나 스플랫을 싣지 않으면 복원한 지형이 디스크 값이라 비교가 진다. 쓰지 않은 지형은 버전과 빈 표시만 싣는다.
 */
SW_TEST_CASE( TerrainTest, UnsavedEditsSurviveStateSnapshot )
{
    const string    heightfieldPath = test::makeTempPath( "snapshot.heightfield" );
    HeightfieldData data            = TerrainTestUtil::makeData( 33, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( heightfieldPath ) );
    constexpr uint32       kSplatSize = 8;
    const LayerWeightsData weights    = TerrainTestUtil::makeUniformWeights( kSplatSize, 0 );
    const vector<uint8>&   splatBytes = weights._bytes;
    const string           splatPath  = test::makeTempPath( "snapshot_splat.layerweights" );
    SW_ASSERT_TRUE( weights.saveToFile( splatPath ) );

    GameObjectManager manager;
    TerrainComponent* arrTerrain[2]{};
    for ( TerrainComponent*& pTerrain : arrTerrain )
    {
        pTerrain = TerrainTestUtil::makeTerrain( manager, "Terrain", ::test::makeTempResourceID( heightfieldPath ), ::test::makeTempResourceID( splatPath ) );
        SW_ASSERT_NOT_NULL( pTerrain );
        SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    }
    TerrainComponent& edited   = *arrTerrain[0];
    TerrainComponent& restored = *arrTerrain[1];

    // 편집이 없으면 싣는 것이 작다(버전 + 빈 표시 둘).
    Archive untouched;
    edited.writeState( untouched );
    vector<uint8> untouchedBytes;
    untouched.writeData( untouchedBytes );
    SW_EXPECT_TRUE( untouchedBytes.size() < 16u );

    vector<float32> listHeight = edited.getHeightfield().getHeightSamples();
    listHeight[5 * 33 + 6]     = 6.5f;
    TerrainSampleRect heightRect;
    heightRect.include( 6, 5 );
    SW_ASSERT_TRUE( edited.writeHeights( listHeight, heightRect ) );
    vector<SurfaceLayerWeights> listWeight;
    SW_ASSERT_TRUE( edited.getHeightfield().readSplatWeights( listWeight ) );
    listWeight[2 * kSplatSize + 3] = SurfaceLayerWeights{
        float4{0.0f, 0.25f,  0.0f, 0.0f},
        float4{0.0f,  0.0f, 0.75f, 0.0f}
    }; // 레이어 1 과 6
    TerrainSampleRect splatRect;
    splatRect.include( 3, 2 );
    SW_ASSERT_TRUE( edited.writeLayerWeights( listWeight, splatRect ) );

    Archive state;
    edited.writeState( state );
    vector<uint8> stateBytes;
    state.writeData( stateBytes );
    restored.restoreState( std::move( stateBytes ) );
    SW_EXPECT_TRUE( restored.getHeightfield().getHeightSamples() == edited.getHeightfield().getHeightSamples() );
    SW_EXPECT_TRUE( restored.getHeightfield().getSplatBytes() == edited.getHeightfield().getSplatBytes() );
    SW_EXPECT_TRUE( restored.getHeightfield().getSplatBytes() != splatBytes );

    SW_TEST_DEFENSIVE_SCOPE( "a snapshot of another heightfield size is rejected and the asset values stay" );
    const string    otherPath = test::makeTempPath( "snapshot_other.heightfield" );
    HeightfieldData other     = TerrainTestUtil::makeData( 17, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( other.saveToFile( otherPath ) );
    restored.setHeightfieldPath( ::test::makeTempResourceID( otherPath ) );
    restored.setSize( float2{ 16.0f, 16.0f } );
    SW_ASSERT_TRUE( restored.reloadTerrain() );
    const vector<float32> listAsset = restored.getHeightfield().getHeightSamples();
    Archive               again;
    edited.writeState( again );
    vector<uint8> againBytes;
    again.writeData( againBytes );
    restored.restoreState( std::move( againBytes ) );
    SW_EXPECT_TRUE( restored.getHeightfield().getHeightSamples() == listAsset );
}

/**
 * @brief [TerrainTest] 다시 표본화 — 평면은 어느 해상도로 옮겨도 같은 평면이고, 구멍은 새 셀 중심이 든 원본 셀을 따른다 · 마스크 쓰기는 사각형만 바꾼다
 * @details 변이: `resampleHeights` 가 원본 샘플 번호를 해상도 비로 옮기지 않으면 평면 비교가 진다. `writeHoleCells` 가 빈 마스크를 0 으로 채우지 않으면 쓰기가 범위를 벗어난다.
 */
SW_TEST_CASE( TerrainTest, ResampleKeepsPlaneAndHoleCells )
{
    constexpr uint32 kSource = 5;
    vector<float32>  listPlane( kSource * kSource );
    for ( uint32 sampleZ = 0; sampleZ < kSource; ++sampleZ )
    {
        for ( uint32 sampleX = 0; sampleX < kSource; ++sampleX )
        {
            listPlane[sampleZ * kSource + sampleX] = static_cast<float32>( sampleX ) + 2.0f * static_cast<float32>( sampleZ );
        }
    }
    vector<float32> listResampled;
    SW_ASSERT_TRUE( TerrainHeightfield::resampleHeights( listPlane, kSource, 9, listResampled ) );
    SW_ASSERT_EQUAL( 81u, static_cast<uint32>( listResampled.size() ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f + 2.0f * 1.5f, listResampled[3 * 9 + 1], 1.0e-5f ); // 새 샘플 (1, 3) 은 원본 (0.5, 1.5)
    SW_EXPECT_NEAR_EQUAL( 4.0f + 8.0f, listResampled[80], 1.0e-5f );

    // 원본 셀 (1, 2) 만 구멍 — 두 배 해상도에서는 셀 (2..3, 4..5) 넷이다.
    vector<uint8> listHole( 4 * 4, 0u );
    listHole[2 * 4 + 1] = 1u;
    vector<uint8> listHoleResampled;
    SW_ASSERT_TRUE( TerrainHeightfield::resampleHoles( listHole, kSource, 9, listHoleResampled ) );
    uint32 holeCount{ 0 };
    for ( uint8 cell : listHoleResampled )
    {
        holeCount += cell != 0 ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 4u, holeCount );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listHoleResampled[5 * 8 + 3] ) );
    vector<uint8> listNoHole;
    SW_EXPECT_TRUE( TerrainHeightfield::resampleHoles( {}, kSource, 9, listNoHole ) && listNoHole.empty() );

    // 마스크 쓰기 — 빈 마스크(구멍 없음)에서 사각형만 켜진다.
    TerrainHeightfield field = TerrainTestUtil::makeWaveField( 9 );
    vector<uint8>      listCell;
    SW_ASSERT_TRUE( field.readHoleCells( listCell ) );
    SW_ASSERT_EQUAL( 64u, static_cast<uint32>( listCell.size() ) );
    listCell[3 * 8 + 2] = 1u;
    listCell[0]         = 1u; // 사각형 밖 — 쓰이지 않는다
    TerrainSampleRect cellRect;
    cellRect.include( 2, 3 );
    SW_ASSERT_TRUE( field.writeHoleCells( listCell, cellRect ) );
    SW_EXPECT_TRUE( field.isHoleCell( 2, 3 ) );
    SW_EXPECT_FALSE( field.isHoleCell( 0, 0 ) );
    float32 height{ 0.0f };
    SW_EXPECT_FALSE( field.findHeightAt( 2.5f, 3.5f, height ) );

    SW_TEST_DEFENSIVE_SCOPE( "wrong sizes and out-of-grid rectangles are rejected without changes" );
    SW_EXPECT_FALSE( TerrainHeightfield::resampleHeights( listPlane, 4, 9, listResampled ) );
    SW_EXPECT_FALSE( TerrainHeightfield::resampleHoles( listHole, kSource, 1, listHoleResampled ) );
    TerrainSampleRect outside;
    outside.include( 8, 8 );
    SW_EXPECT_FALSE( field.writeHoleCells( listCell, outside ) );
    SW_EXPECT_FALSE( field.writeHoleCells( vector<uint8>( 3, 1u ), cellRect ) );
}

/**
 * @brief [TerrainTest] 구멍 쓰기와 크기 바꾸기 — 질의 · 청크 세대가 따라가고, 크기를 바꾼 저장하지 않은 편집은 상태 스냅샷으로 같은 에셋을 다시 읽은 지형에 돌아간다
 * @details 변이: `writeHoles` 가 청크 세대를 올리지 않으면 물리 바디가 낡은 채 남아 세대 비교가 진다. `writeState` 가 크기 바꾸기 표시를 싣지 않으면 복원한 지형이
 *          에셋 해상도(33)로 남는다. 청크 칸으로 나뉘지 않는 해상도는 거절된다.
 */
SW_TEST_CASE( TerrainTest, WriteHolesAndResizeSurviveStateSnapshot )
{
    const string    heightfieldPath = test::makeTempPath( "holes_resize.heightfield" );
    HeightfieldData data            = TerrainTestUtil::makeData( 33, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( heightfieldPath ) );

    GameObjectManager manager;
    TerrainComponent* arrTerrain[2]{};
    for ( TerrainComponent*& pTerrain : arrTerrain )
    {
        pTerrain = TerrainTestUtil::makeTerrain( manager, "Terrain", ::test::makeTempResourceID( heightfieldPath ), "" );
        SW_ASSERT_NOT_NULL( pTerrain );
        SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    }
    TerrainComponent& edited   = *arrTerrain[0];
    TerrainComponent& restored = *arrTerrain[1];

    // 32 m · 33 샘플이라 셀 한 칸이 1 m 다. 셀 (20, 3) 은 청크 (1, 0) 에만 든다.
    vector<uint8> listHole;
    SW_ASSERT_TRUE( edited.getHeightfield().readHoleCells( listHole ) );
    listHole[3 * 32 + 20] = 1u;
    TerrainSampleRect cellRect;
    cellRect.include( 20, 3 );
    const vector<uint32> listGenerationBefore = edited.getChunkGenerations();
    SW_ASSERT_TRUE( edited.writeHoles( listHole, cellRect ) );
    float32 height{ 0.0f };
    SW_EXPECT_FALSE( edited.findHeightAt( 20.5f, 3.5f, height ) );
    SW_EXPECT_TRUE( edited.findHeightAt( 19.5f, 3.5f, height ) );
    SW_EXPECT_TRUE( edited.getChunkGenerations()[1] > listGenerationBefore[1] );
    SW_EXPECT_EQUAL( listGenerationBefore[0], edited.getChunkGenerations()[0] );

    // 크기 바꾸기 — 65 샘플은 청크 칸 16 으로 나뉘고(청크 4 × 4), 20 은 나뉘지 않는다.
    float32 heightBefore{ 0.0f };
    SW_ASSERT_TRUE( edited.findHeightAt( 7.0f, 9.0f, heightBefore ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a resolution the chunk cells do not divide is rejected without changes" );
        SW_EXPECT_FALSE( edited.resampleHeightfield( 20 ) );
        SW_EXPECT_EQUAL( 33u, edited.getHeightfield().getResolution() );
    }
    SW_ASSERT_TRUE( edited.resampleHeightfield( 65 ) );
    SW_EXPECT_EQUAL( 65u, edited.getHeightfield().getResolution() );
    SW_EXPECT_EQUAL( 16u, edited.getChunkLayout().getChunkCount() );
    SW_EXPECT_EQUAL( 16u, static_cast<uint32>( edited.getChunkGenerations().size() ) );
    float32 heightAfter{ 0.0f };
    SW_ASSERT_TRUE( edited.findHeightAt( 7.0f, 9.0f, heightAfter ) ); // 원본 샘플 위의 점은 그대로다
    SW_EXPECT_NEAR_EQUAL( heightBefore, heightAfter, 1.0e-4f );
    SW_EXPECT_FALSE( edited.findHeightAt( 20.5f, 3.5f, height ) ); // 구멍도 따라 옮겨진다

    Archive state;
    edited.writeState( state );
    vector<uint8> stateBytes;
    state.writeData( stateBytes );
    restored.restoreState( std::move( stateBytes ) );
    SW_EXPECT_EQUAL( 65u, restored.getHeightfield().getResolution() );
    SW_EXPECT_TRUE( restored.getHeightfield().getHeightSamples() == edited.getHeightfield().getHeightSamples() );
    SW_EXPECT_TRUE( restored.getHeightfield().getHoleCells() == edited.getHeightfield().getHoleCells() );
}

/**
 * @brief [TerrainTest] 프로퍼티 변경 · 오너 이동이 부르는 다시 읽기는 저장하지 않은 편집을 넘기고, `reloadTerrain` 을 직접 부르면 디스크 값으로 돌아간다
 * @details 변이: `onPropertyChanged` 가 `reloadTerrain` 을 부르면 쓴 높이가 사라져 첫 비교가 진다(에디터 플레이 시작 · 지형 옮기기에서 칠한 것을 잃던 결함).
 */
SW_TEST_CASE( TerrainTest, PropertyChangeKeepsUnsavedEdits )
{
    const string    heightfieldPath = test::makeTempPath( "keep.heightfield" );
    HeightfieldData data            = TerrainTestUtil::makeData( 33, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( heightfieldPath ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Terrain" ) );
    SW_ASSERT_NOT_NULL( pObject );
    TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );
    pTerrain->setHeightfieldPath( ::test::makeTempResourceID( heightfieldPath ) );
    pTerrain->setSize( float2{ 32.0f, 32.0f } );
    pTerrain->setHeightRange( TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax );
    pTerrain->setChunkCells( 16 );
    pTerrain->setMaterialPath( "" );
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    const vector<float32> listAsset = pTerrain->getHeightfield().getHeightSamples();

    vector<float32> listHeight = listAsset;
    listHeight[5 * 33 + 6]     = 6.5f;
    TerrainSampleRect rect;
    rect.include( 6, 5 );
    SW_ASSERT_TRUE( pTerrain->writeHeights( listHeight, rect ) );
    const vector<float32> listEdited = pTerrain->getHeightfield().getHeightSamples();

    pTerrain->onPropertyChanged( hashed_string( "_lodDistance" ) );
    SW_EXPECT_TRUE( pTerrain->hasWrittenEdits() );
    SW_EXPECT_TRUE( pTerrain->getHeightfield().getHeightSamples() == listEdited );

    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    SW_EXPECT_FALSE( pTerrain->hasWrittenEdits() );
    SW_EXPECT_TRUE( pTerrain->getHeightfield().getHeightSamples() == listAsset );
}

/**
 * @brief [TerrainTest] 큰 사각형 높이 쓰기 — 청크 메시는 쓰기 한 번에 넷까지만 다시 만들고 나머지는 틱(`rebuildStaleChunkMeshes`)이 나눠 만든다
 */
SW_TEST_CASE( TerrainTest, LargeHeightWriteSpreadsChunkRebuilds )
{
    const string    path = test::makeTempPath( "edit_large.heightfield" );
    HeightfieldData data = TerrainTestUtil::makeData( 65, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( path ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Terrain" ) );
    SW_ASSERT_NOT_NULL( pObject );
    TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );
    pTerrain->setHeightfieldPath( ::test::makeTempResourceID( path ) );
    pTerrain->setSize( float2{ 64.0f, 64.0f } );
    pTerrain->setHeightRange( TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax );
    pTerrain->setChunkCells( 16 );
    pTerrain->setMaterialPath( "" );
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );

    vector<float32>   listHeight = pTerrain->getHeightfield().getHeightSamples();
    TerrainSampleRect rect;
    rect.include( 0, 0 );
    rect.include( 64, 64 ); // 청크 16 개 전부
    SW_ASSERT_TRUE( pTerrain->writeHeights( listHeight, rect ) );
    SW_EXPECT_EQUAL( 12u, pTerrain->getStaleChunkCount() );
    SW_EXPECT_EQUAL( 4u, pTerrain->rebuildStaleChunkMeshes( 4 ) );
    SW_EXPECT_EQUAL( 8u, pTerrain->getStaleChunkCount() );
    SW_EXPECT_EQUAL( 8u, pTerrain->rebuildStaleChunkMeshes( 100 ) );
    SW_EXPECT_EQUAL( 0u, pTerrain->getStaleChunkCount() );

    // 브러시 한 번(청크 하나 안)은 바로 끝난다.
    TerrainSampleRect brush;
    brush.include( 5, 5 );
    brush.include( 7, 7 );
    SW_ASSERT_TRUE( pTerrain->writeHeights( listHeight, brush ) );
    SW_EXPECT_EQUAL( 0u, pTerrain->getStaleChunkCount() );
}

/**
 * @brief [TerrainTest] 레이어 가중치 쓰기 —스플랫이 없으면 거절 · 사각형 안만 양자화해 쓰고 읽으면 보이며 · 크기가 틀리면 거절
 */
SW_TEST_CASE( TerrainTest, WriteLayerWeightsUpdatesSplat )
{
    TerrainHeightfield          field = TerrainTestUtil::makeWaveField( 5 );
    vector<SurfaceLayerWeights> listWeight;
    SW_EXPECT_FALSE( field.readSplatWeights( listWeight ) );
    SW_EXPECT_FALSE( field.writeSplatWeights( listWeight, TerrainSampleRect{ 0, 0, 1, 1 } ) );

    vector<uint8> bytes( 4 * 4 * SurfaceLayerWeights::kLayerCount, 0u );
    for ( size_t texel = 0; texel < 16; ++texel )
    {
        bytes[texel * SurfaceLayerWeights::kLayerCount] = 255u; // 전부 레이어 0
    }
    field.setSplat( 4, 4, bytes );
    SW_ASSERT_TRUE( field.readSplatWeights( listWeight ) );
    SW_ASSERT_EQUAL( 16u, static_cast<uint32>( listWeight.size() ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listWeight[0]._low._x, 1.0e-5f );

    listWeight[1 * 4 + 1] = SurfaceLayerWeights{
        float4{0.0f, 0.25f,  0.0f, 0.0f},
        float4{0.0f,  0.0f, 0.75f, 0.0f}
    }; // 레이어 1 과 6
    listWeight[0] = SurfaceLayerWeights{
        float4{ 0.0f, 1.0f, 0.0f, 0.0f },
        float4{}
    }; // 사각형 밖 — 쓰이지 않는다
    TerrainSampleRect rect;
    rect.include( 1, 1 );
    SW_ASSERT_TRUE( field.writeSplatWeights( listWeight, rect ) );
    vector<SurfaceLayerWeights> listRead;
    SW_ASSERT_TRUE( field.readSplatWeights( listRead ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, listRead[1 * 4 + 1].get( 1 ), 1.0f / 255.0f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, listRead[1 * 4 + 1].get( 6 ), 1.0f / 255.0f ); // 높은 레이어도 양자화해 쓰고 읽는다
    SW_EXPECT_NEAR_EQUAL( 1.0f, listRead[0]._low._x, 1.0e-5f );

    SW_TEST_DEFENSIVE_SCOPE( "wrong size or out-of-grid rectangles are rejected" );
    vector<SurfaceLayerWeights> listShort( 3 );
    SW_EXPECT_FALSE( field.writeSplatWeights( listShort, rect ) );
    TerrainSampleRect outside;
    outside.include( 3, 3 );
    outside.include( 4, 4 );
    SW_EXPECT_FALSE( field.writeSplatWeights( listWeight, outside ) );
}

/**
 * @brief [TerrainTest] 지형마다 읽을 때 가중치 바이트로 자기 스플랫 텍스처를 만든다 — 칠하기는 그 텍스처만 고쳐 같은 에셋을 쓰는 다른 지형은 디스크 값 그대로 ·
 *        텍스처 원본은 GPU 없이도 칠한 값을 따라간다(디바이스를 다시 만들면 이 원본을 올린다) · `reloadTerrain` 은 옛 텍스처를 캐시에서 놓고 새로 만든다
 * @details 변이: 두 지형이 텍스처 하나를 나눠 쓰면 경로가 같아 첫 단언이 지고, 흘려보내기가 CPU 텍스처 원본을 고치지 않으면 바이트 비교가 진다.
 */
SW_TEST_CASE( TerrainTest, PaintedSplatIsPerTerrainAndKeptOnCPU )
{
    SW_ASSERT_TRUE( engine::areEngineServicesBound() );
    const string    heightfieldPath = test::makeTempPath( "paint.heightfield" );
    HeightfieldData data            = TerrainTestUtil::makeData( 33, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( heightfieldPath ) );
    constexpr uint32       kSplatSize = 8;
    const LayerWeightsData weights    = TerrainTestUtil::makeUniformWeights( kSplatSize, 0 ); // 전부 레이어 0
    const vector<uint8>&   splatBytes = weights._bytes;
    const string           splatPath  = test::makeTempPath( "paint_splat.layerweights" );
    SW_ASSERT_TRUE( weights.saveToFile( splatPath ) );

    GameObjectManager manager;
    TerrainComponent* arrTerrain[2]{};
    const utf8*       arrName[2] = { "PaintedTerrain", "OtherTerrain" };
    for ( uint32 index = 0; index < 2; ++index )
    {
        arrTerrain[index] = TerrainTestUtil::makeTerrain( manager, arrName[index], ::test::makeTempResourceID( heightfieldPath ), ::test::makeTempResourceID( splatPath ) );
        SW_ASSERT_NOT_NULL( arrTerrain[index] );
        SW_ASSERT_TRUE( arrTerrain[index]->reloadTerrain() );
    }
    TerrainComponent& painted  = *arrTerrain[0];
    TerrainComponent& other    = *arrTerrain[1];
    TextureCache&     textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
    (void)textures.flushRegionUpdates( nullptr ); // 앞선 테스트가 남긴 구간을 비운다

    const string ownPath   = painted.getCPUSplatPath( 0 );
    const string ownHigh   = painted.getCPUSplatPath( 1 );
    const string otherPath = other.getCPUSplatPath( 0 );
    SW_ASSERT_TRUE( TextureCache::isCPUTexturePath( ownPath ) );
    SW_ASSERT_TRUE( TextureCache::isCPUTexturePath( otherPath ) );
    SW_EXPECT_NOT_EQUAL( ownPath, otherPath );
    const Texture2D* pTexture      = textures.find( ownPath );
    const Texture2D* pOtherTexture = textures.find( otherPath );
    SW_ASSERT_NOT_NULL( pTexture );
    SW_ASSERT_NOT_NULL( pOtherTexture );
    SW_EXPECT_TRUE( pTexture->getCPUBytes() == TerrainTestUtil::planeBytes( splatBytes, 0 ) );

    vector<SurfaceLayerWeights> listWeight;
    SW_ASSERT_TRUE( painted.getHeightfield().readSplatWeights( listWeight ) );
    listWeight[3 * kSplatSize + 2] = SurfaceLayerWeights{
        float4{ 0.0f, 1.0f, 0.0f, 0.0f },
        float4{}
    };
    TerrainSampleRect first;
    first.include( 2, 3 );
    SW_ASSERT_TRUE( painted.writeLayerWeights( listWeight, first ) );
    listWeight[5 * kSplatSize + 5] = SurfaceLayerWeights{
        float4{},
        float4{ 0.0f, 0.0f, 1.0f, 0.0f }
    }; // 레이어 6 - 높은 평면 텍스처가 따라가야 한다
    TerrainSampleRect second;
    second.include( 5, 5 );
    SW_ASSERT_TRUE( painted.writeLayerWeights( listWeight, second ) );
    SW_EXPECT_EQUAL( ownPath, painted.getCPUSplatPath( 0 ) );

    // 쓰기마다 구간 하나가 같은 텍스처로 간다. GPU 가 없어도 원본은 따라간다.
    SW_EXPECT_EQUAL( 4u, textures.flushRegionUpdates( nullptr ) ); // 쓰기 둘 × 평면 둘
    SW_EXPECT_TRUE( pTexture->getCPUBytes() == TerrainTestUtil::planeBytes( painted.getHeightfield().getSplatBytes(), 0 ) );
    const Texture2D* pHighTexture = textures.find( ownHigh );
    SW_ASSERT_NOT_NULL( pHighTexture );
    SW_EXPECT_TRUE( pHighTexture->getCPUBytes() == TerrainTestUtil::planeBytes( painted.getHeightfield().getSplatBytes(), 1 ) );
    SW_EXPECT_TRUE( pHighTexture->getCPUBytes() != TerrainTestUtil::planeBytes( splatBytes, 1 ) );
    SW_EXPECT_TRUE( other.getHeightfield().getSplatBytes() == splatBytes );
    SW_EXPECT_TRUE( pOtherTexture->getCPUBytes() == TerrainTestUtil::planeBytes( splatBytes, 0 ) );

    // 다시 읽으면 디스크 가중치로 돌아가고, 옛 텍스처는 캐시에서 빠지고 새 텍스처를 만든다(빌린 머티리얼이 없다).
    SW_ASSERT_TRUE( painted.reloadTerrain() );
    SW_EXPECT_FALSE( textures.isCached( ownPath ) );
    const Texture2D* pReloaded = textures.find( painted.getCPUSplatPath( 0 ) );
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_EXPECT_TRUE( pReloaded->getCPUBytes() == TerrainTestUtil::planeBytes( splatBytes, 0 ) );
}

/**
 * @brief [TerrainTest] 가중치 형식(`.layerweights`)은 쓰고 읽으면 같다 · 매직 · 판 · 레이어 수 · 크기가 틀리면 읽지 않는다
 */
SW_TEST_CASE( TerrainTest, LayerWeightsFormatRoundTrips )
{
    LayerWeightsData data;
    data._width  = 5;
    data._height = 3;
    data._bytes.assign( 5 * 3 * LayerWeightsData::kLayerCount, 0u );
    for ( size_t index = 0; index < data._bytes.size(); ++index )
    {
        data._bytes[index] = static_cast<uint8>( index * 7u ); // 가로와 세로가 바뀌면 다른 바이트가 된다
    }
    vector<uint8> bytes;
    data.saveToMemory( bytes );
    LayerWeightsData loaded;
    SW_ASSERT_TRUE( loaded.loadFromMemory( bytes, "roundtrip" ) );
    SW_EXPECT_EQUAL( 5u, loaded._width );
    SW_EXPECT_EQUAL( 3u, loaded._height );
    SW_EXPECT_TRUE( loaded._bytes == data._bytes );

    SW_TEST_DEFENSIVE_SCOPE( "malformed layer weight bytes are rejected" );
    vector<uint8> truncated( bytes.begin(), bytes.end() - 1 );
    SW_EXPECT_FALSE( loaded.loadFromMemory( truncated, "truncated" ) );
    SW_EXPECT_TRUE( loaded._bytes.empty() );
    vector<uint8> badMagic = bytes;
    badMagic[0]            = 'X';
    SW_EXPECT_FALSE( loaded.loadFromMemory( badMagic, "magic" ) );
    vector<uint8> badVersion = bytes;
    badVersion[4]            = 9u;
    SW_EXPECT_FALSE( loaded.loadFromMemory( badVersion, "version" ) );
    vector<uint8> badLayerCount = bytes;
    badLayerCount[16]           = 3u;
    SW_EXPECT_FALSE( loaded.loadFromMemory( badLayerCount, "layers" ) );
    LayerWeightsData empty;
    SW_EXPECT_FALSE( empty.saveToFile( test::makeTempPath( "empty.layerweights" ) ) );
}

/**
 * @brief [TerrainTest] 레이어 넷 시절의 가중치 파일(버전 1)은 읽을 때 레이어 4..7 을 0 으로 채워 이관하고, 다시 쓰면 현재 버전이다 · 버전과 레이어 수가 맞지 않으면 거절한다
 * @details 변이: 이관이 텍셀 사이 간격을 넷으로 두면(복사 폭 오류) 둘째 텍셀의 값이 어긋나 바이트 비교가 진다.
 */
SW_TEST_CASE( TerrainTest, LayerWeightsLegacyVersionIsMigrated )
{
    vector<uint8> legacy;
    LittleEndianUtil::appendUint32( legacy, LayerWeightsData::kMagic );
    LittleEndianUtil::appendUint32( legacy, LayerWeightsData::kLegacyVersion );
    LittleEndianUtil::appendUint32( legacy, 2u ); // 너비
    LittleEndianUtil::appendUint32( legacy, 1u ); // 높이
    LittleEndianUtil::appendUint32( legacy, LayerWeightsData::kLegacyLayerCount );
    const vector<uint8> weights = { 10, 20, 30, 40, 50, 60, 70, 80 };
    legacy.insert( legacy.end(), weights.begin(), weights.end() );

    LayerWeightsData loaded;
    SW_ASSERT_TRUE( loaded.loadFromMemory( legacy, "legacy" ) );
    const vector<uint8> expected = { 10, 20, 30, 40, 0, 0, 0, 0, 50, 60, 70, 80, 0, 0, 0, 0 };
    SW_EXPECT_TRUE( loaded._bytes == expected );
    SW_EXPECT_TRUE( loaded.isValid() );

    vector<uint8> rewritten;
    loaded.saveToMemory( rewritten );
    SW_EXPECT_EQUAL( LayerWeightsData::kVersion, LittleEndianUtil::readUint32( rewritten.data() + 4 ) );
    SW_EXPECT_EQUAL( LayerWeightsData::kLayerCount, LittleEndianUtil::readUint32( rewritten.data() + 16 ) );

    SW_TEST_DEFENSIVE_SCOPE( "a legacy version with the current layer count is rejected" );
    vector<uint8> mismatched = legacy;
    mismatched[16]           = static_cast<uint8>( LayerWeightsData::kLayerCount );
    SW_EXPECT_FALSE( loaded.loadFromMemory( mismatched, "mismatch" ) );
}

/**
 * @brief [TerrainTest] 전용 서버 호스트(`ResourcePaths::setHostTarget( "Server" )`)도 가중치 에셋을 읽어 레이어 질의에 정확히 답한다 — 레이어 0 이 아니다 ·
 *        가중치를 못 읽으면(서버가 읽지 않는 텍스처 경로, 없는 파일) 레이어 0 으로 두지 않고 지형을 읽지 않는다
 * @details 변이: 가중치를 서버가 빼는 종류(텍스처)로 두면 서버에서 읽기가 조용히 실패해 첫 단언이 지고(F-1 의 옛 동작), 읽지 못한 가중치를 레이어 0 으로 두면
 *          `reloadTerrain` 이 true 라 마지막 단언들이 진다.
 */
SW_TEST_CASE( TerrainTest, DedicatedServerAnswersLayerQueries )
{
    const string    heightfieldPath = test::makeTempPath( "server.heightfield" );
    HeightfieldData data            = TerrainTestUtil::makeData( 33, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( heightfieldPath ) );
    const string weightsPath = test::makeTempPath( "server.layerweights" );
    SW_ASSERT_TRUE( TerrainTestUtil::makeUniformWeights( 8, 2 ).saveToFile( weightsPath ) ); // 전부 레이어 2(바위)

    test::ScopedHostTarget serverHost( "Server" );
    GameObjectManager      manager;
    TerrainComponent*      pTerrain = TerrainTestUtil::makeTerrain( manager, "ServerTerrain", ::test::makeTempResourceID( heightfieldPath ), ::test::makeTempResourceID( weightsPath ) );
    SW_ASSERT_NOT_NULL( pTerrain );
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    const SurfaceLayerWeights weight = pTerrain->computeLayerWeightsAt( 16.0f, 16.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, weight._low._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, weight._low._z, 1e-4f );

    SW_TEST_DEFENSIVE_SCOPE( "layer weights the server cannot read stop the terrain instead of answering layer 0" );
    const string ddsPath = test::makeTempPath( "server_splat.dds" );
    SW_ASSERT_TRUE( ImageFileWriter::writeDDSRgba8( ddsPath, vector<uint8>( 8 * 8 * 4, 0u ), 8, 8 ) );
    pTerrain->setLayerWeightsPath( ::test::makeTempResourceID( ddsPath ) );
    SW_EXPECT_FALSE( pTerrain->reloadTerrain() );
    SW_EXPECT_FALSE( pTerrain->getHeightfield().isValid() );
    pTerrain->setLayerWeightsPath( ::test::makeTempResourceID( test::makeTempPath( "missing.layerweights" ) ) );
    SW_EXPECT_FALSE( pTerrain->reloadTerrain() );
}
