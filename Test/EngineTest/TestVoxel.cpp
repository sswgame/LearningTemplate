#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Voxel/VoxelBlock.h"
#include "GameFramework/Kits/Voxel/VoxelBody.h"
#include "GameFramework/Kits/Voxel/VoxelHotbar.h"
#include "GameFramework/Kits/Voxel/VoxelMesher.h"
#include "GameFramework/Kits/Voxel/VoxelRaycast.h"
#include "GameFramework/Kits/Voxel/VoxelTerrain.h"
#include "GameFramework/Kits/Voxel/VoxelWorld.h"

#include "TestFramework/TestFramework.h"

// 복셀 키트(마인크래프트 장르) — 블록 카탈로그, 청크 월드와 이웃 청크 다시 짓기, 씨앗 고정 지형, 격자 광선, 드러난 면만 짓는 메싱(감김 · 그늘),
// 상자 몸의 착지 · 점프 · 벽 미끄러짐, 핫바.

using namespace sw;

namespace
{
    constexpr const utf8* kVoxelTestBlockXml = R"(
<BlockCatalog atlasColumns="4" atlasRows="4" tileTexels="16">
  <Block id="grass" name="Grass" tile="2" top="0" side="1" hardness="0.6"/>
  <Block id="dirt" tile="2"/>
  <Block id="stone" tile="3" hardness="1.5" color="0.9 0.9 0.9 1"/>
  <Block id="sand" tile="4"/>
  <Block id="water" tile="5" solid="false" opaque="false" color="0.4 0.6 1 0.6" breakable="false"/>
  <Block id="bedrock" tile="6" breakable="false"/>
  <Block id="log" tile="7" top="8" bottom="8"/>
  <Block id="leaves" tile="9" opaque="false"/>
  <Block name="NoId"/>
</BlockCatalog>
)";

    /** @brief 시험 카탈로그와 그것을 빌려 쓰는 월드입니다. */
    struct VoxelTestScene
    {
        VoxelBlockCatalog _catalog;
        VoxelWorld        _world;

        bool initialize( int32 chunkCountX, int32 chunkCountZ )
        {
            if ( _catalog.loadFromXmlText( kVoxelTestBlockXml, "VoxelTest" ) == false )
                return false;
            _world.initialize( chunkCountX, chunkCountZ, &_catalog );
            return true;
        }

        VoxelBlockIndex findBlock( const utf8* pId ) const { return _catalog.findBlockIndex( hashed_string( pId ) ); }

        /** @brief y = @p floorY 높이에 돌 바닥을 깝니다. */
        void fillFloor( int32 floorY )
        {
            const VoxelBlockIndex stone = findBlock( "stone" );
            for ( int32 z = 0; z < _world.getSizeZ(); ++z )
            {
                for ( int32 x = 0; x < _world.getSizeX(); ++x )
                    (void)_world.setBlock( x, floorY, z, stone );
            }
        }
    };

    /** @brief 삼각형이 노멀 쪽을 앞면으로 보는지(`(b - a) × (c - a)` · n > 0) 봅니다. */
    bool isTriangleOutward( const VoxelMeshVertex& a, const VoxelMeshVertex& b, const VoxelMeshVertex& c )
    {
        return ( b._position - a._position ).cross( c._position - a._position ).dot( a._normal ) > 0.0f;
    }

    bool areAllTrianglesOutward( const vector<VoxelMeshVertex>& listVertex )
    {
        for ( size_t vertexIndex = 0; vertexIndex + 2 < listVertex.size(); vertexIndex += 3 )
        {
            if ( isTriangleOutward( listVertex[vertexIndex], listVertex[vertexIndex + 1], listVertex[vertexIndex + 2] ) == false )
                return false;
        }
        return true;
    }
} // namespace

/**
 * @brief [VoxelTest] 블록 번호는 읽은 순서로 1 부터, 면마다 아틀라스 칸(tile 기본 · top · bottom · side 덮어쓰기) · 물은 통과 · 반투명, UV 는 반 텍셀 들인다
 */
SW_TEST_CASE( VoxelTest, CatalogReadsFacesAndAssignsIndices )
{
    VoxelTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 1, 1 ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 8 ), scene._catalog.getBlocks().size() );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( scene.findBlock( "grass" ) ) );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( scene.findBlock( "water" ) ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( scene.findBlock( "lava" ) ) );

    const VoxelBlockDef* pGrass = scene._catalog.findBlock( scene.findBlock( "grass" ) );
    SW_ASSERT_NOT_NULL( pGrass );
    SW_EXPECT_EQUAL( 0, pGrass->_arrFaceTile[static_cast<int32>( VoxelFace::PositiveY )] );
    SW_EXPECT_EQUAL( 2, pGrass->_arrFaceTile[static_cast<int32>( VoxelFace::NegativeY )] );
    SW_EXPECT_EQUAL( 1, pGrass->_arrFaceTile[static_cast<int32>( VoxelFace::NegativeZ )] );
    SW_EXPECT_FALSE( scene._catalog.isSolid( scene.findBlock( "water" ) ) );
    SW_EXPECT_FALSE( scene._catalog.isOpaque( scene.findBlock( "leaves" ) ) );
    SW_EXPECT_TRUE( scene._catalog.isSolid( scene.findBlock( "leaves" ) ) );
    SW_EXPECT_FALSE( scene._catalog.isSolid( kVoxelAirBlock ) );

    float2 uvMin;
    float2 uvMax;
    scene._catalog.computeTileUv( 5, uvMin, uvMax ); // 4 × 4 의 (1, 1) 칸
    SW_EXPECT_NEAR_EQUAL( 0.25f + 0.25f / 32.0f, uvMin._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f + 0.25f / 32.0f, uvMin._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f - 0.25f / 32.0f, uvMax._x, 1.0e-6f );
}

/**
 * @brief [VoxelTest] 월드 밖은 공기로 읽히고 쓰기는 거절된다 · 블록이 바뀌면 그 청크와 경계면 이웃 청크가 다시 지을 것으로 표시된다 · 아래는 막혀 있다
 */
SW_TEST_CASE( VoxelTest, WorldMarksNeighbourChunksWhenBorderBlocksChange )
{
    VoxelTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 3, 3 ) );
    const VoxelBlockIndex stone = scene.findBlock( "stone" );
    SW_EXPECT_FALSE( scene._world.setBlock( -1, 5, 5, stone ) );
    SW_EXPECT_FALSE( scene._world.setBlock( 5, kVoxelChunkHeight, 5, stone ) );
    SW_EXPECT_TRUE( scene._world.getBlock( 100, 5, 5 ) == kVoxelAirBlock );
    SW_EXPECT_TRUE( scene._world.isSolid( 5, -1, 5 ) );

    for ( int32 chunkZ = 0; chunkZ < 3; ++chunkZ )
    {
        for ( int32 chunkX = 0; chunkX < 3; ++chunkX )
            scene._world.clearChunkDirty( chunkX, chunkZ );
    }
    SW_ASSERT_TRUE( scene._world.setBlock( 20, 5, 20, stone ) ); // 가운데 청크의 안쪽
    SW_EXPECT_TRUE( scene._world.isChunkDirty( 1, 1 ) );
    SW_EXPECT_FALSE( scene._world.isChunkDirty( 0, 1 ) );
    scene._world.clearChunkDirty( 1, 1 );

    SW_ASSERT_TRUE( scene._world.setBlock( 16, 5, 31, stone ) ); // 가운데 청크의 왼쪽 · 앞 경계
    SW_EXPECT_TRUE( scene._world.isChunkDirty( 1, 1 ) );
    SW_EXPECT_TRUE( scene._world.isChunkDirty( 0, 1 ) );
    SW_EXPECT_TRUE( scene._world.isChunkDirty( 1, 2 ) );
    SW_EXPECT_FALSE( scene._world.isChunkDirty( 2, 1 ) );
    SW_EXPECT_FALSE( scene._world.isChunkDirty( 1, 0 ) );
    SW_EXPECT_EQUAL( 2u, scene._world.countBlocks( stone ) );
    SW_EXPECT_EQUAL( 5, scene._world.findTopSolidY( 20, 20 ) );
    SW_EXPECT_EQUAL( -1, scene._world.findTopSolidY( 0, 0 ) );
}

/**
 * @brief [VoxelTest] 지형은 씨앗이 같으면 같고 다르면 다르다 · 기둥은 기반암 → 돌 → 흙 → 풀(물가는 모래) · 물 높이 아래 빈 칸은 물 · 나무가 선다
 */
SW_TEST_CASE( VoxelTest, TerrainIsDeterministicAndLayered )
{
    VoxelTestScene first;
    VoxelTestScene second;
    VoxelTestScene other;
    SW_ASSERT_TRUE( first.initialize( 4, 4 ) );
    SW_ASSERT_TRUE( second.initialize( 4, 4 ) );
    SW_ASSERT_TRUE( other.initialize( 4, 4 ) );
    VoxelTerrainSettings     settings;
    const VoxelTerrainReport firstReport  = VoxelTerrainGenerator::fillWorld( first._world, settings );
    const VoxelTerrainReport secondReport = VoxelTerrainGenerator::fillWorld( second._world, settings );
    settings._seed                        = 4242u;
    (void)VoxelTerrainGenerator::fillWorld( other._world, settings );

    const VoxelBlockIndex arrBlock[] = { first.findBlock( "grass" ), first.findBlock( "stone" ), first.findBlock( "water" ), first.findBlock( "log" ) };
    bool                  bSame      = true;
    bool                  bDifferent = false;
    for ( const VoxelBlockIndex block : arrBlock )
    {
        bSame      = bSame && first._world.countBlocks( block ) == second._world.countBlocks( block );
        bDifferent = bDifferent || first._world.countBlocks( block ) != other._world.countBlocks( block );
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( bDifferent );
    SW_EXPECT_EQUAL( firstReport._treeCount, secondReport._treeCount );
    SW_EXPECT_TRUE( firstReport._treeCount > 0u );
    SW_EXPECT_TRUE( firstReport._maxHeight > firstReport._minHeight );
    SW_EXPECT_TRUE( first._world.isChunkDirty( 3, 3 ) );

    // 기둥 하나를 위에서 아래로 — 지면 높이는 월드 없이도 같은 값이다.
    settings._seed       = 1337u;
    bool bLayersHold     = true;
    bool bSawWaterColumn = false;
    for ( int32 x = 0; x < first._world.getSizeX(); x += 7 )
    {
        const int32           height = VoxelTerrainGenerator::computeSurfaceHeight( x, 9, settings );
        const VoxelBlockIndex top    = first._world.getBlock( x, height, 9 );
        bLayersHold                  = bLayersHold && ( top == first.findBlock( "grass" ) || top == first.findBlock( "sand" ) );
        bLayersHold                  = bLayersHold && first._world.getBlock( x, 0, 9 ) == first.findBlock( "bedrock" );
        bLayersHold                  = bLayersHold && first._world.getBlock( x, 1, 9 ) == first.findBlock( "stone" );
        if ( height + 1 < settings._waterLevel )
        {
            bSawWaterColumn = true;
            bLayersHold     = bLayersHold && first._world.getBlock( x, height + 1, 9 ) == first.findBlock( "water" );
        }
    }
    SW_EXPECT_TRUE( bLayersHold );
    (void)bSawWaterColumn; // 씨앗에 따라 이 줄에 물이 없을 수 있다 — 물 블록 수는 위에서 견줬다
}

/**
 * @brief [VoxelTest] 광선은 첫 단단한 블록의 가까운 면에서 맞고(놓을 자리 = 그 앞 칸), 물은 지나며, 사거리 · 월드 밖은 놓친다 · 모서리를 스쳐도 칸을 건너뛰지 않는다
 */
SW_TEST_CASE( VoxelTest, RaycastFindsTheFaceAndThePlacementCell )
{
    VoxelTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 1, 1 ) );
    const VoxelBlockIndex stone = scene.findBlock( "stone" );
    const VoxelBlockIndex water = scene.findBlock( "water" );
    SW_ASSERT_TRUE( scene._world.setBlock( 5, 5, 5, stone ) );
    SW_ASSERT_TRUE( scene._world.setBlock( 5, 5, 3, water ) );

    VoxelRayHit hit;
    SW_ASSERT_TRUE( VoxelRaycast::raycast( scene._world, float3{ 5.5f, 5.5f, 0.5f }, float3{ 0.0f, 0.0f, 1.0f }, 10.0f, hit ) );
    SW_EXPECT_TRUE( hit._block == ( VoxelCoord{ 5, 5, 5 } ) );
    SW_EXPECT_TRUE( hit._previous == ( VoxelCoord{ 5, 5, 4 } ) );
    SW_EXPECT_TRUE( hit._normal == ( VoxelCoord{ 0, 0, -1 } ) );
    SW_EXPECT_NEAR_EQUAL( 4.5f, hit._distance, 1.0e-4f );
    SW_EXPECT_TRUE( hit._blockIndex == stone );

    SW_ASSERT_TRUE( VoxelRaycast::raycast( scene._world, float3{ 5.5f, 5.5f, 0.5f }, float3{ 0.0f, 0.0f, 1.0f }, 10.0f, hit, true ) );
    SW_EXPECT_TRUE( hit._block == ( VoxelCoord{ 5, 5, 3 } ) ); // 물에서 멈추게 할 수도 있다(양동이)
    SW_EXPECT_FALSE( VoxelRaycast::raycast( scene._world, float3{ 5.5f, 5.5f, 0.5f }, float3{ 0.0f, 0.0f, 1.0f }, 4.0f, hit ) );
    SW_EXPECT_FALSE( VoxelRaycast::raycast( scene._world, float3{ 5.5f, 5.5f, 0.5f }, float3{ 0.0f, 0.0f, -1.0f }, 10.0f, hit ) );

    // 위에서 내려다보며 비스듬히 — 윗면에서 맞는다.
    SW_ASSERT_TRUE( VoxelRaycast::raycast( scene._world, float3{ 3.5f, 8.5f, 5.5f }, float3{ 1.0f, -1.5f, 0.0f }, 10.0f, hit ) );
    SW_EXPECT_TRUE( hit._block == ( VoxelCoord{ 5, 5, 5 } ) );
    SW_EXPECT_TRUE( hit._normal == ( VoxelCoord{ 0, 1, 0 } ) );

    // 대각선이 블록 모서리 근처를 지나도 그 칸을 본다.
    SW_ASSERT_TRUE( scene._world.setBlock( 9, 5, 9, stone ) );
    SW_ASSERT_TRUE( VoxelRaycast::raycast( scene._world, float3{ 7.5f, 5.5f, 7.51f }, float3{ 1.0f, 0.0f, 1.0f }, 10.0f, hit ) );
    SW_EXPECT_TRUE( hit._block == ( VoxelCoord{ 9, 5, 9 } ) );
}

/**
 * @brief [VoxelTest] 메시는 드러난 면만(외딴 블록 6 면 · 붙은 둘 10 면 · 물 옆 물은 안 그림), 삼각형은 모두 바깥 앞면, 바닥에 닿은 옆면의 아래 꼭짓점은 그늘진다
 */
SW_TEST_CASE( VoxelTest, MesherEmitsExposedFacesWithOutwardWindingAndOcclusion )
{
    VoxelTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 2, 1 ) );
    const VoxelBlockIndex stone = scene.findBlock( "stone" );
    const VoxelBlockIndex water = scene.findBlock( "water" );
    VoxelChunkMesh        mesh;

    SW_ASSERT_TRUE( scene._world.setBlock( 5, 10, 5, stone ) );
    VoxelMesher::fillChunkMesh( scene._world, 0, 0, mesh );
    SW_EXPECT_EQUAL( 6u, mesh.getFaceCount() );
    SW_EXPECT_TRUE( areAllTrianglesOutward( mesh._listOpaqueVertex ) );
    bool bInsideBlock = true;
    for ( const VoxelMeshVertex& vertex : mesh._listOpaqueVertex )
    {
        bInsideBlock = bInsideBlock && vertex._position._x >= 5.0f && vertex._position._x <= 6.0f && vertex._position._y >= 10.0f && vertex._position._y <= 11.0f;
        bInsideBlock = bInsideBlock && vertex._uv._x > 0.75f && vertex._uv._x < 1.0f; // 칸 3 = (3, 0)
    }
    SW_EXPECT_TRUE( bInsideBlock );

    SW_ASSERT_TRUE( scene._world.setBlock( 6, 10, 5, stone ) );
    VoxelMesher::fillChunkMesh( scene._world, 0, 0, mesh );
    SW_EXPECT_EQUAL( 10u, mesh.getFaceCount() );

    // 청크 경계 — 이웃 청크의 블록이 이 청크의 면을 가린다. 정점은 그 청크 원점 기준이다.
    SW_ASSERT_TRUE( scene._world.setBlock( 15, 20, 5, stone ) );
    SW_ASSERT_TRUE( scene._world.setBlock( 16, 20, 5, stone ) );
    VoxelChunkMesh neighborMesh;
    VoxelMesher::fillChunkMesh( scene._world, 1, 0, neighborMesh );
    SW_EXPECT_EQUAL( 5u, neighborMesh.getFaceCount() );
    bool bLocal = true;
    for ( const VoxelMeshVertex& vertex : neighborMesh._listOpaqueVertex )
        bLocal = bLocal && vertex._position._x >= 0.0f && vertex._position._x <= 1.0f;
    SW_EXPECT_TRUE( bLocal );

    // 물 둘은 서로 사이 면이 없고 반투명 목록으로 간다.
    SW_ASSERT_TRUE( scene._world.setBlock( 2, 30, 2, water ) );
    SW_ASSERT_TRUE( scene._world.setBlock( 3, 30, 2, water ) );
    VoxelMesher::fillChunkMesh( scene._world, 0, 0, mesh );
    SW_EXPECT_EQUAL( static_cast<size_t>( 10 * 6 ), mesh._listTranslucentVertex.size() );
    SW_EXPECT_TRUE( areAllTrianglesOutward( mesh._listTranslucentVertex ) );

    // 바닥 위 블록의 옆면 — 아래 꼭짓점이 위 꼭짓점보다 어둡다.
    VoxelTestScene floorScene;
    SW_ASSERT_TRUE( floorScene.initialize( 1, 1 ) );
    floorScene.fillFloor( 3 );
    SW_ASSERT_TRUE( floorScene._world.setBlock( 8, 4, 8, stone ) );
    VoxelMesher::fillChunkMesh( floorScene._world, 0, 0, mesh );
    float32 lowShade  = 1.0f;
    float32 highShade = 0.0f;
    for ( const VoxelMeshVertex& vertex : mesh._listOpaqueVertex )
    {
        const bool bSideOfRaisedBlock = vertex._normal._x > 0.5f && MathUtil::abs( vertex._position._x - 9.0f ) < 1.0e-4f && vertex._position._y >= 4.0f;
        if ( bSideOfRaisedBlock == false )
            continue;
        if ( vertex._position._y < 4.5f )
            lowShade = MathUtil::min( lowShade, vertex._color._x );
        else
            highShade = MathUtil::max( highShade, vertex._color._x );
    }
    SW_EXPECT_TRUE( lowShade < highShade );
    SW_EXPECT_TRUE( areAllTrianglesOutward( mesh._listOpaqueVertex ) );
}

/**
 * @brief [VoxelTest] 몸은 떨어져 바닥 위에 서고, 한 블록은 뛰어넘지만 두 블록은 못 넘으며, 벽에 비스듬히 가면 벽을 따라 미끄러진다
 */
SW_TEST_CASE( VoxelTest, BodyLandsJumpsOneBlockAndSlidesAlongWalls )
{
    VoxelTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 2, 2 ) );
    scene.fillFloor( 9 ); // 바닥 윗면 y = 10
    VoxelBody body;
    body.setPosition( float3{ 8.5f, 15.0f, 8.5f } );
    for ( int32 frameIndex = 0; frameIndex < 120; ++frameIndex )
        body.step( scene._world, float3{ 0.0f, 0.0f, 0.0f }, false, false, 1.0f / 60.0f );
    SW_EXPECT_TRUE( body.isOnGround() );
    SW_EXPECT_NEAR_EQUAL( 10.0f, body.getPosition()._y, 0.01f );

    // 제자리 점프 — 꼭대기가 1.25 m 남짓.
    float32 apex = 0.0f;
    for ( int32 frameIndex = 0; frameIndex < 90; ++frameIndex )
    {
        body.step( scene._world, float3{ 0.0f, 0.0f, 0.0f }, frameIndex == 0, false, 1.0f / 60.0f );
        apex = MathUtil::max( apex, body.getPosition()._y - 10.0f );
    }
    SW_EXPECT_TRUE( apex > 1.05f && apex < 1.5f );
    SW_EXPECT_TRUE( body.isOnGround() );

    // +X 로 걸으며 계속 뛰면 한 칸 단은 오르고 두 칸 벽에서 멈춘다.
    const VoxelBlockIndex stone = scene.findBlock( "stone" );
    for ( int32 z = 0; z < scene._world.getSizeZ(); ++z )
    {
        SW_ASSERT_TRUE( scene._world.setBlock( 12, 10, z, stone ) );
        SW_ASSERT_TRUE( scene._world.setBlock( 16, 11, z, stone ) );
        SW_ASSERT_TRUE( scene._world.setBlock( 16, 12, z, stone ) );
    }
    for ( int32 x = 13; x < 16; ++x )
    {
        for ( int32 z = 0; z < scene._world.getSizeZ(); ++z )
            SW_ASSERT_TRUE( scene._world.setBlock( x, 10, z, stone ) );
    }
    for ( int32 frameIndex = 0; frameIndex < 240; ++frameIndex )
        body.step( scene._world, float3{ 1.0f, 0.0f, 0.0f }, true, false, 1.0f / 60.0f );
    SW_EXPECT_TRUE( body.getPosition()._y >= 11.0f - 0.01f ); // 단 위로 올라왔다
    SW_EXPECT_TRUE( body.getPosition()._x < 16.0f - 0.29f );  // 벽 앞에서 멈췄다
    SW_EXPECT_TRUE( body.getPosition()._x > 15.0f );

    // 벽에 비스듬히(+X +Z) — X 는 막히고 Z 로 미끄러진다.
    const float32 startZ = body.getPosition()._z;
    for ( int32 frameIndex = 0; frameIndex < 60; ++frameIndex )
        body.step( scene._world, float3{ 0.7071f, 0.0f, 0.7071f }, false, false, 1.0f / 60.0f );
    SW_EXPECT_TRUE( body.getPosition()._z - startZ > 2.0f );
    SW_EXPECT_TRUE( body.getPosition()._x < 16.0f - 0.29f );

    SW_EXPECT_TRUE( body.overlapsBlock( VoxelCoord{ static_cast<int32>( body.getPosition()._x ), 11, static_cast<int32>( body.getPosition()._z ) } ) );
    SW_EXPECT_FALSE( body.overlapsBlock( VoxelCoord{ static_cast<int32>( body.getPosition()._x ), 10, static_cast<int32>( body.getPosition()._z ) } ) );
}

/**
 * @brief [VoxelTest] 핫바는 같은 블록 칸부터 64 개까지 채우고 넘치면 빈 칸으로 · 가득 차면 남은 수를 돌려준다 · 고른 칸에서 하나씩 꺼낸다 · 휠은 감긴다
 */
SW_TEST_CASE( VoxelTest, HotbarStacksAndConsumes )
{
    VoxelHotbar hotbar;
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( 3, 10 ) );
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( 4, 1 ) );
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( 3, 60 ) );
    SW_EXPECT_EQUAL( 64, hotbar.getSlot( 0 )._count );
    SW_EXPECT_EQUAL( 6, hotbar.getSlot( 2 )._count );
    SW_EXPECT_EQUAL( 70, hotbar.countBlock( 3 ) );
    SW_EXPECT_EQUAL( 65, hotbar.addBlock( 5, 64 * 7 + 1 ) ); // 빈 칸 여섯에 384 개
    SW_EXPECT_EQUAL( 64 * 6, hotbar.countBlock( 5 ) );

    hotbar.select( 1 );
    VoxelBlockIndex block = kVoxelAirBlock;
    SW_EXPECT_TRUE( hotbar.consumeSelected( block ) );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( block ) );
    SW_EXPECT_FALSE( hotbar.consumeSelected( block ) );
    SW_EXPECT_TRUE( hotbar.getSelectedSlot().isEmpty() );
    SW_EXPECT_EQUAL( 1, hotbar.addBlock( 6, 65 ) ); // 빈 칸은 1 번 하나 — 64 만 들어간다
    hotbar.selectRelative( -2 );
    SW_EXPECT_EQUAL( 8, hotbar.getSelectedIndex() );
    hotbar.selectRelative( 3 );
    SW_EXPECT_EQUAL( 2, hotbar.getSelectedIndex() );
}
