#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Environment/Terrain/TerrainMeshBuilder.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Scene/GpuSceneBuilder.h"
#include "Engine/Scene/Scene.h"

#include "TestFramework/TestFramework.h"

// 지형 — 높이장 형식 · 해석 곡면 대비 높이 · 노멀 · 구멍 · 스플랫 정규화 · 청크 LOD 의 틈 없음 · 컴포넌트 LOD 갱신.

using namespace sw;

namespace
{
    struct TerrainTestUtil
    {
        static constexpr float32 kHeightMin = -10.0f;
        static constexpr float32 kHeightMax = 10.0f;

        /** @brief 해석 곡면 h(x, z) 를 해상도 @p resolution · 칸 1 m 로 굽습니다. */
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
                uint32        bitsY{ 0 };
                uint32        bitsZ{ 0 };
                std::memcpy( &bitsY, &worldY, sizeof( bitsY ) );
                std::memcpy( &bitsZ, &worldZ, sizeof( bitsZ ) );
                outListKey.push_back( ( static_cast<uint64>( bitsZ ) << 32 ) | bitsY );
            }
            std::sort( outListKey.begin(), outListKey.end() );
            outListKey.erase( std::unique( outListKey.begin(), outListKey.end() ), outListKey.end() );
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
    TerrainMeshBuilder::buildChunkVertices( field, layout, 0, 0, 0, arrNeighbor, listVertex );
    SW_EXPECT_NEAR_EQUAL( 16.0f * 16.0f - 1.0f, TerrainTestUtil::computeCoveredArea( listVertex ), 1.0e-3f );
}

/**
 * @brief [TerrainTest] 스플랫 가중치는 합이 1 로 나뉜다(셰이더와 같은 식) · 모두 0 이면 레이어 0 이다 · 텍셀 중심이 지형 가장자리다
 */
SW_TEST_CASE( TerrainTest, SplatWeightsNormalize )
{
    const float4 zero = TerrainHeightfield::normalizeWeights( float4{ 0.0f, 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 1.0f, zero._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, zero._y + zero._z + zero._w, 1.0e-6f );

    TerrainHeightfield field = TerrainTestUtil::makeWaveField( 17 );
    // 3 × 2 스플랫: 아무 값(합이 1 이 아님) · 모두 0 칸 하나.
    const vector<uint8> rgbaBytes = { 200, 40, 0, 0, 10, 10, 10, 10, 0, 0, 0, 0, 0, 255, 255, 0, 90, 0, 30, 120, 7, 3, 1, 250 };
    field.setSplat( 3, 2, rgbaBytes );
    float32 worstSum{ 0.0f };
    for ( uint32 probe = 0; probe < 64; ++probe )
    {
        const float4  weight = field.computeLayerWeightsAt( static_cast<float32>( probe % 8 ) * 2.1f, static_cast<float32>( probe / 8 ) * 2.05f );
        const float32 sum    = weight._x + weight._y + weight._z + weight._w;
        worstSum             = MathUtil::max( worstSum, MathUtil::abs( sum - 1.0f ) );
    }
    SW_EXPECT_TRUE( worstSum < 1.0e-5f );
    // 모서리 (0, 0) 는 첫 텍셀 그대로(200, 40) → (0.833, 0.167).
    const float4 corner = field.computeLayerWeightsAtUv( 0.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 200.0f / 240.0f, corner._x, 1.0e-5f );
    // 셋째 텍셀(u = 1, v = 0)은 모두 0 → 레이어 0.
    const float4 empty = field.computeLayerWeightsAtUv( 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, empty._x, 1.0e-5f );
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
            TerrainMeshBuilder::buildChunkVertices( field, layout, 1, 1, leftLOD, arrLeftNeighbor, listLeft );
            TerrainMeshBuilder::buildChunkVertices( field, layout, 2, 1, rightLOD, arrRightNeighbor, listRight );
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
    pTerrain->setHeightfieldPath( path );
    pTerrain->setSize( float2{ 64.0f, 64.0f } );
    pTerrain->setHeightRange( TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax );
    pTerrain->setChunkCells( 16 );
    pTerrain->setLODDistance( 20.0f );
    pTerrain->setMaterialPath( "" ); // 그리지 않는 시험 — 머티리얼을 잡지 않는다
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
SW_TEST_CASE( TerrainTest, ChunksLeaveTheGpuSceneWhenTheComponentOrOwnerIsOff )
{
    const string    path = test::makeTempPath( "toggle.heightfield" );
    HeightfieldData data = TerrainTestUtil::makeData( 33, &TerrainTestUtil::wave );
    SW_ASSERT_TRUE( data.saveToFile( path ) );

    Scene scene( "TerrainToggle" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    GameObject*       pObject  = scene.getObjectManager()->createGameObject( hashed_string( "Terrain" ) );
    TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );
    pTerrain->setHeightfieldPath( path );
    pTerrain->setSize( float2{ 32.0f, 32.0f } );
    pTerrain->setHeightRange( TerrainTestUtil::kHeightMin, TerrainTestUtil::kHeightMax );
    pTerrain->setChunkCells( 16 );
    pTerrain->setMaterialPath( "" );
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    const uint32 chunkCount = pTerrain->getChunkLayout().getChunkCount();
    SW_ASSERT_TRUE( chunkCount > 0 );

    GpuSceneBuilder gpuScene;
    const float3    camPos{ 0.0f, 50.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos );
    SW_ASSERT_EQUAL( chunkCount, static_cast<uint32>( gpuScene.getInstances().size() ) );

    pTerrain->setActive( false );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    pTerrain->setActive( true );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( chunkCount, static_cast<uint32>( gpuScene.getInstances().size() ) );

    pObject->setActive( false );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    pObject->setActive( true );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( chunkCount, static_cast<uint32>( gpuScene.getInstances().size() ) );
}
