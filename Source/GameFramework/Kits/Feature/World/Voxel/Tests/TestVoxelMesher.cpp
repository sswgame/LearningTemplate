#include "pch.h"

#include "Core/Container/set.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"
#include "GameFramework/Kits/Feature/World/Voxel/View/VoxelMesher.h"

using namespace sw;

namespace
{
    /** @brief 돌(불투명, 칸 3) · 유리(반투명, 칸 5) 카탈로그와 2 x 2 청크 월드입니다(공기 생성 훅으로 네 청크가 모두 올라와 있다). */
    struct VoxelMesherFixture
    {
        VoxelBlockCatalog _catalog;
        VoxelWorld        _world;
        VoxelBlockIndex   _stone{ kVoxelAirBlock };
        VoxelBlockIndex   _glass{ kVoxelAirBlock };

        VoxelMesherFixture()
        {
            _stone = addBlock( "stone", true, 3, 1.0f );
            _glass = addBlock( "glass", false, 5, 0.5f );
            SW_REQUIRE_TRUE( _world.initialize( 2, 2, &_catalog ) );
            _world.setChunkGenerator( []( const VoxelWorld&, int32, int32, vector<VoxelBlockIndex>& ) {} );
            SW_REQUIRE_EQUAL( uint32( 4 ), _world.updateResidency( 0, 0, 1, 4 ) );
        }

        VoxelBlockIndex addBlock( const utf8* pID, bool bOpaque, int32 tile, float32 alpha )
        {
            VoxelBlockDef block;
            block._id      = hashed_string( pID );
            block._name    = pID;
            block._bOpaque = bOpaque;
            block._color   = float4{ 1.0f, 1.0f, 1.0f, alpha };
            for ( int32& faceTile : block._arrFaceTile )
            {
                faceTile = tile;
            }
            return _catalog.addBlock( block );
        }
    };

    /** @brief 면 하나 — (면, 청크 지역 블록 좌표)입니다. */
    struct VoxelFaceCell
    {
        int32 _face{ 0 };
        int32 _x{ 0 };
        int32 _y{ 0 };
        int32 _z{ 0 };

        bool operator<( const VoxelFaceCell& other ) const
        {
            if ( _face != other._face )
                return _face < other._face;
            if ( _x != other._x )
                return _x < other._x;
            if ( _y != other._y )
                return _y < other._y;
            return _z < other._z;
        }
    };

    struct VoxelMesherTestUtil
    {
        /** @brief 블록마다 여섯 면을 따로 보던 옛 메셔의 드러난 면 집합입니다(정본 규칙: 공기 옆 · 불투명이 아닌 다른 블록 옆이면 드러난다). */
        static set<VoxelFaceCell> collectExpectedFaces( const VoxelWorld& world, int32 chunkX, int32 chunkZ )
        {
            set<VoxelFaceCell>       setFace;
            const VoxelBlockCatalog& catalog = *world.getCatalog();
            for ( int32 y = 0; y < kVoxelChunkHeight; ++y )
            {
                for ( int32 z = 0; z < kVoxelChunkSize; ++z )
                {
                    for ( int32 x = 0; x < kVoxelChunkSize; ++x )
                    {
                        const VoxelCoord      coord{ chunkX * kVoxelChunkSize + x, y, chunkZ * kVoxelChunkSize + z };
                        const VoxelBlockIndex block = world.getBlock( coord );
                        if ( catalog.findBlock( block ) == nullptr )
                            continue;
                        for ( int32 face = 0; face < kVoxelFaceCount; ++face )
                        {
                            const VoxelBlockIndex neighbor = world.getBlock( coord + getVoxelFaceOffset( static_cast<VoxelFace>( face ) ) );
                            const bool            bShown   = neighbor == kVoxelAirBlock || ( catalog.isOpaque( neighbor ) == false && neighbor != block );
                            if ( bShown )
                                setFace.insert( VoxelFaceCell{ face, x, y, z } );
                        }
                    }
                }
            }
            return setFace;
        }

        static bool isSameFaces( const set<VoxelFaceCell>& expected, const set<VoxelFaceCell>& actual )
        {
            if ( expected.size() != actual.size() )
                return false;
            for ( const VoxelFaceCell& cell : expected )
            {
                if ( actual.find( cell ) == actual.end() )
                    return false;
            }
            return true;
        }

        static int32 findFace( const float3& normal )
        {
            for ( int32 face = 0; face < kVoxelFaceCount; ++face )
            {
                const VoxelCoord offset = getVoxelFaceOffset( static_cast<VoxelFace>( face ) );
                if ( static_cast<float32>( offset._x ) == normal._x && static_cast<float32>( offset._y ) == normal._y && static_cast<float32>( offset._z ) == normal._z )
                    return face;
            }
            return -1;
        }

        /**
         * @brief 메시 사각형(정점 여섯)마다 덮는 면을 펼쳐 @p inoutFaces 에 넣습니다. 겹쳐 덮은 면이 있으면 false 입니다.
         *        타일이 늘어나지 않았는지도 봅니다 — 면 안 블록 좌표의 폭 × 높이가 사각형 넓이와 같아야 하고, 타일 칸이 그 자리 블록의 칸이어야 한다.
         */
        static bool collectMeshFaces( const VoxelWorld& world, int32 chunkX, int32 chunkZ, const vector<VoxelMeshVertex>& listVertex, set<VoxelFaceCell>& inoutFaces,
                                      uint32& outQuadCount )
        {
            const VoxelBlockCatalog& catalog = *world.getCatalog();
            for ( size_t first = 0; first + 6 <= listVertex.size(); first += 6 )
            {
                ++outQuadCount;
                float3 minPosition = listVertex[first]._position;
                float3 maxPosition = minPosition;
                float2 minUV       = listVertex[first]._uv;
                float2 maxUV       = minUV;
                for ( size_t index = first; index < first + 6; ++index )
                {
                    const VoxelMeshVertex& vertex = listVertex[index];
                    minPosition                   = float3{ MathUtil::min( minPosition._x, vertex._position._x ), MathUtil::min( minPosition._y, vertex._position._y ),
                                          MathUtil::min( minPosition._z, vertex._position._z ) };
                    maxPosition                   = float3{ MathUtil::max( maxPosition._x, vertex._position._x ), MathUtil::max( maxPosition._y, vertex._position._y ),
                                          MathUtil::max( maxPosition._z, vertex._position._z ) };
                    minUV                         = float2{ MathUtil::min( minUV._x, vertex._uv._x ), MathUtil::min( minUV._y, vertex._uv._y ) };
                    maxUV                         = float2{ MathUtil::max( maxUV._x, vertex._uv._x ), MathUtil::max( maxUV._y, vertex._uv._y ) };
                }
                const int32 face = findFace( listVertex[first]._normal );
                if ( face < 0 )
                    return false;
                const VoxelCoord normal = getVoxelFaceOffset( static_cast<VoxelFace>( face ) );
                const int32      minX   = static_cast<int32>( minPosition._x ) - ( normal._x > 0 ? 1 : 0 );
                const int32      minY   = static_cast<int32>( minPosition._y ) - ( normal._y > 0 ? 1 : 0 );
                const int32      minZ   = static_cast<int32>( minPosition._z ) - ( normal._z > 0 ? 1 : 0 );
                const int32      sizeX  = normal._x != 0 ? 1 : static_cast<int32>( maxPosition._x - minPosition._x );
                const int32      sizeY  = normal._y != 0 ? 1 : static_cast<int32>( maxPosition._y - minPosition._y );
                const int32      sizeZ  = normal._z != 0 ? 1 : static_cast<int32>( maxPosition._z - minPosition._z );
                const float32    area   = static_cast<float32>( sizeX * sizeY * sizeZ );
                if ( ( maxUV._x - minUV._x ) * ( maxUV._y - minUV._y ) != area )
                    return false; // 타일이 블록마다 되풀이되지 않고 늘어났다
                const VoxelBlockDef* pBlock =
                    catalog.findBlock( world.getBlock( chunkX * kVoxelChunkSize + minX, minY, chunkZ * kVoxelChunkSize + minZ ) );
                if ( pBlock == nullptr )
                    return false;
                const int32 tile = pBlock->_arrFaceTile[face];
                if ( static_cast<int32>( minUV._x / VoxelMesher::kTileUVStride ) != tile % catalog.getAtlasColumns() ||
                     static_cast<int32>( minUV._y / VoxelMesher::kTileUVStride ) != tile / catalog.getAtlasColumns() )
                    return false;
                for ( int32 y = minY; y < minY + sizeY; ++y )
                {
                    for ( int32 z = minZ; z < minZ + sizeZ; ++z )
                    {
                        for ( int32 x = minX; x < minX + sizeX; ++x )
                        {
                            if ( inoutFaces.insert( VoxelFaceCell{ face, x, y, z } ).second == false )
                                return false;
                        }
                    }
                }
            }
            return true;
        }
    };
} // namespace

// 그리디 메싱은 면을 합칠 뿐 덮는 표면은 블록마다 면을 내던 메셔와 같아야 한다 — 면 집합이 같고(빠짐 · 겹침 없음), 넓이 합 = 블록 면 수, 타일은 늘지 않는다.
SW_TEST_CASE( VoxelMesherTest, GreedyMeshCoversTheSameFacesAsPerBlockFaces )
{
    VoxelMesherFixture rig;
    for ( int32 z = 0; z < rig._world.getSizeZ(); ++z )
    {
        for ( int32 x = 0; x < rig._world.getSizeX(); ++x )
        {
            SW_REQUIRE_TRUE( rig._world.setBlock( x, 2, z, rig._stone ) );
            // 씨앗 없는 흩뿌림 — 기둥 · 구멍 · 유리가 섞여 그늘이 고르지 않은 면과 청크 경계 면이 생긴다
            const uint32 hash = ( static_cast<uint32>( x ) * 73856093u ) ^ ( static_cast<uint32>( z ) * 19349663u );
            if ( hash % 7u == 0u )
                SW_REQUIRE_TRUE( rig._world.setBlock( x, 3, z, rig._stone ) );
            if ( hash % 11u == 0u )
                SW_REQUIRE_TRUE( rig._world.setBlock( x, 2, z, kVoxelAirBlock ) );
            if ( hash % 13u == 0u )
                SW_REQUIRE_TRUE( rig._world.setBlock( x, 4, z, rig._glass ) );
        }
    }
    for ( int32 chunkZ = 0; chunkZ < 2; ++chunkZ )
    {
        for ( int32 chunkX = 0; chunkX < 2; ++chunkX )
        {
            VoxelChunkMesh mesh;
            VoxelMesher::fillChunkMesh( rig._world, chunkX, chunkZ, mesh );
            set<VoxelFaceCell> setMeshFace;
            uint32             quadCount = 0;
            SW_REQUIRE_TRUE( VoxelMesherTestUtil::collectMeshFaces( rig._world, chunkX, chunkZ, mesh._listOpaqueVertex, setMeshFace, quadCount ) );
            SW_REQUIRE_TRUE( VoxelMesherTestUtil::collectMeshFaces( rig._world, chunkX, chunkZ, mesh._listTranslucentVertex, setMeshFace, quadCount ) );
            const set<VoxelFaceCell> setExpected = VoxelMesherTestUtil::collectExpectedFaces( rig._world, chunkX, chunkZ );
            SW_EXPECT_EQUAL( setExpected.size(), setMeshFace.size() );
            SW_EXPECT_TRUE( VoxelMesherTestUtil::isSameFaces( setExpected, setMeshFace ) );
            SW_EXPECT_TRUE( quadCount < setExpected.size() ); // 합친 면이 있다
        }
    }
}

// 고른 그늘의 평평한 바닥은 면마다 사각형 하나다 — 블록마다 면을 내면 윗면 256 · 아랫면 256 · 월드 끝 옆면 32 개다.
SW_TEST_CASE( VoxelMesherTest, FlatFloorIsOneQuadPerSide )
{
    VoxelMesherFixture rig;
    for ( int32 z = 0; z < rig._world.getSizeZ(); ++z )
    {
        for ( int32 x = 0; x < rig._world.getSizeX(); ++x )
        {
            SW_REQUIRE_TRUE( rig._world.setBlock( x, 2, z, rig._stone ) );
        }
    }
    VoxelChunkMesh mesh;
    VoxelMesher::fillChunkMesh( rig._world, 0, 0, mesh );
    SW_EXPECT_EQUAL( size_t( 4 * 6 ), mesh._listOpaqueVertex.size() ); // 위 · 아래 · 월드 끝 -X · -Z
    SW_EXPECT_TRUE( mesh._listTranslucentVertex.empty() );
    // 윗면 사각형의 블록 좌표는 0 ~ 16 이다 — 타일이 16 번 되풀이된다
    bool bFoundTop = false;
    for ( const VoxelMeshVertex& vertex : mesh._listOpaqueVertex )
    {
        if ( vertex._normal._y <= 0.5f )
            continue;
        bFoundTop            = true;
        const float32 localU = vertex._uv._x - 3.0f * VoxelMesher::kTileUVStride;
        SW_EXPECT_TRUE( localU == 0.0f || localU == static_cast<float32>( kVoxelChunkSize ) );
    }
    SW_EXPECT_TRUE( bFoundTop );
}

// 올라오지 않은 이웃 청크는 공기로 읽힌다 — 그 경계 면이 드러난다. 그래서 게임은 상주 반경보다 한 칸 안쪽 청크만 그린다(VoxelGameModeComponent).
SW_TEST_CASE( VoxelMesherTest, MissingNeighborChunkReadsAsAir )
{
    VoxelMesherFixture rig;
    for ( int32 z = 0; z < rig._world.getSizeZ(); ++z )
    {
        for ( int32 x = 0; x < rig._world.getSizeX(); ++x )
        {
            SW_REQUIRE_TRUE( rig._world.setBlock( x, 2, z, rig._stone ) );
        }
    }
    (void)rig._world.updateResidency( 0, 0, 0, 1 ); // (0, 0) 만 남는다
    SW_REQUIRE_EQUAL( uint32( 1 ), rig._world.getResidentChunkCount() );
    VoxelChunkMesh mesh;
    VoxelMesher::fillChunkMesh( rig._world, 0, 0, mesh );
    SW_EXPECT_EQUAL( size_t( 6 * 6 ), mesh._listOpaqueVertex.size() ); // 위 · 아래 · 네 옆면(+X · +Z 는 내려간 이웃 쪽)
}
