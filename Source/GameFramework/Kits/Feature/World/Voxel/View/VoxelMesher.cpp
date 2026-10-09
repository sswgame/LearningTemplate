#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/View/VoxelMesher.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

namespace sw
{
    namespace
    {
        /** @brief 면 하나의 기하 — 원점 칸 꼭짓점과 두 접선(u × v = 노멀)입니다. 꼭짓점 순서 o, o+u, o+u+v, o+v 가 바깥에서 본 앞면 감김입니다. */
        struct VoxelFaceFrame
        {
            VoxelCoord _origin;
            VoxelCoord _tangentU;
            VoxelCoord _tangentV;
        };

        struct VoxelMesherInternal
        {
            static constexpr VoxelFaceFrame kArrFaceFrame[kVoxelFaceCount] = {
                {{ 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }}, // +X : Y × Z = X
                {{ 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }}, // -X : Z × Y = -X
                {{ 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }}, // +Y : Z × X = Y
                {{ 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }}, // -Y : X × Z = -Y
                {{ 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }}, // +Z : X × Y = Z
                {{ 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }}, // -Z : Y × X = -Z
            };

            static constexpr VoxelCoord scale( const VoxelCoord& coord, int32 factor ) { return VoxelCoord{ coord._x * factor, coord._y * factor, coord._z * factor }; }

            static float3 toFloat3( const VoxelCoord& coord )
            {
                return float3{ static_cast<float32>( coord._x ), static_cast<float32>( coord._y ), static_cast<float32>( coord._z ) };
            }

            /** @brief 면이 드러나는지 봅니다. */
            static bool isFaceVisible( const VoxelWorld& world, VoxelBlockIndex block, const VoxelCoord& neighbor )
            {
                const VoxelBlockIndex neighborBlock = world.getBlock( neighbor );
                if ( neighborBlock == kVoxelAirBlock )
                    return true;
                if ( world.getCatalog()->isOpaque( neighborBlock ) )
                    return false;
                return neighborBlock != block;
            }

            /** @brief 면 앞 칸이 빛을 막는지(그늘을 만드는지) 봅니다 — 불투명 블록만 그늘을 만든다. */
            static int32 occludes( const VoxelWorld& world, const VoxelCoord& coord ) { return world.isOpaque( coord._x, coord._y, coord._z ) ? 1 : 0; }

            /** @brief 꼭짓점 그늘 단계(0..3)입니다. 옆 둘이 다 막히면 모서리와 상관없이 0 이다. */
            static int32 computeOcclusion( int32 side1, int32 side2, int32 corner )
            {
                if ( side1 != 0 && side2 != 0 )
                    return 0;
                return 3 - ( side1 + side2 + corner );
            }

            /** @brief 면 UV — 옆면은 위가 v 0(텍스처 윗줄이 블록 위), 윗 · 아랫면은 X → u · Z → v 입니다. */
            static float2 computeCornerUv( VoxelFace face, const VoxelCoord& corner, const float2& uvMin, const float2& uvMax )
            {
                float32 u = 0.0f;
                float32 v = 0.0f;
                if ( face == VoxelFace::PositiveY || face == VoxelFace::NegativeY )
                {
                    u = static_cast<float32>( corner._x );
                    v = static_cast<float32>( corner._z );
                }
                else
                {
                    // 바깥에서 볼 때 u 가 오른쪽으로 늘게 — 글자 · 무늬가 거울에 비치지 않는다.
                    if ( face == VoxelFace::PositiveX )
                        u = 1.0f - static_cast<float32>( corner._z );
                    else if ( face == VoxelFace::NegativeX )
                        u = static_cast<float32>( corner._z );
                    else if ( face == VoxelFace::PositiveZ )
                        u = static_cast<float32>( corner._x );
                    else
                        u = 1.0f - static_cast<float32>( corner._x );
                    v = 1.0f - static_cast<float32>( corner._y );
                }
                return float2{ uvMin._x + ( uvMax._x - uvMin._x ) * u, uvMin._y + ( uvMax._y - uvMin._y ) * v };
            }

            static void pushFace( const VoxelWorld& world, const VoxelBlockDef& block, VoxelFace face, const VoxelCoord& worldCoord, const VoxelCoord& chunkOrigin,
                                  vector<VoxelMeshVertex>& outList )
            {
                const VoxelFaceFrame& frame  = kArrFaceFrame[static_cast<int32>( face )];
                const VoxelCoord      normal = getVoxelFaceOffset( face );
                const VoxelCoord      front  = worldCoord + normal;
                float2                uvMin;
                float2                uvMax;
                world.getCatalog()->computeTileUv( block._arrFaceTile[static_cast<int32>( face )], uvMin, uvMax );
                const float32 faceShade = getFaceShadeValue( face );

                // 꼭짓점 0..3 = (u, v) 의 (0,0) (1,0) (1,1) (0,1).
                constexpr int32 kArrCornerU[4] = { 0, 1, 1, 0 };
                constexpr int32 kArrCornerV[4] = { 0, 0, 1, 1 };
                VoxelMeshVertex arrVertex[4];
                int32           arrOcclusion[4] = { 3, 3, 3, 3 };
                for ( int32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
                {
                    const VoxelCoord cornerOffset = frame._origin + scale( frame._tangentU, kArrCornerU[cornerIndex] ) + scale( frame._tangentV, kArrCornerV[cornerIndex] );
                    const VoxelCoord sideU        = scale( frame._tangentU, kArrCornerU[cornerIndex] * 2 - 1 );
                    const VoxelCoord sideV        = scale( frame._tangentV, kArrCornerV[cornerIndex] * 2 - 1 );
                    if ( block._bOpaque != SW_FALSE )
                        arrOcclusion[cornerIndex] = computeOcclusion( occludes( world, front + sideU ), occludes( world, front + sideV ), occludes( world, front + sideU + sideV ) );
                    const float32    shade  = faceShade * VoxelMesher::getOcclusionShade( arrOcclusion[cornerIndex] );
                    VoxelMeshVertex& vertex = arrVertex[cornerIndex];
                    vertex._position        = toFloat3( worldCoord + cornerOffset ) - toFloat3( chunkOrigin );
                    vertex._normal          = toFloat3( normal );
                    vertex._uv              = computeCornerUv( face, cornerOffset, uvMin, uvMax );
                    vertex._color           = float4{ block._color._x * shade, block._color._y * shade, block._color._z * shade, block._color._w };
                }
                // 그늘이 0–2 대각선 쪽으로 쏠렸으면 1–3 대각선으로 자른다(순환 순서라 감김은 그대로).
                const int32     firstCorner           = ( arrOcclusion[0] + arrOcclusion[2] < arrOcclusion[1] + arrOcclusion[3] ) ? 1 : 0;
                constexpr int32 kArrTriangleCorner[6] = { 0, 1, 2, 0, 2, 3 };
                for ( const int32 corner : kArrTriangleCorner )
                {
                    outList.push_back( arrVertex[( corner + firstCorner ) % 4] );
                }
            }

            static constexpr float32 getFaceShadeValue( VoxelFace face )
            {
                switch ( face )
                {
                    case VoxelFace::PositiveY:
                        return 1.0f;
                    case VoxelFace::NegativeY:
                        return 0.6f;
                    case VoxelFace::PositiveZ:
                    case VoxelFace::NegativeZ:
                        return 0.85f;
                    case VoxelFace::PositiveX:
                    case VoxelFace::NegativeX:
                        return 0.75f;
                }
                return 1.0f;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 VoxelMesher::getFaceShade( VoxelFace face )
    {
        return VoxelMesherInternal::getFaceShadeValue( face );
    }

    float32 VoxelMesher::getOcclusionShade( int32 occlusionLevel )
    {
        constexpr float32 kArrShade[4] = { 0.45f, 0.65f, 0.82f, 1.0f };
        return kArrShade[MathUtil::clamp( occlusionLevel, 0, 3 )];
    }

    void VoxelMesher::fillChunkMesh( const VoxelWorld& world, int32 chunkX, int32 chunkZ, VoxelChunkMesh& outMesh )
    {
        outMesh.clear();
        const VoxelBlockCatalog* pCatalog = world.getCatalog();
        if ( pCatalog == nullptr || world.findChunk( chunkX, chunkZ ) == nullptr )
            return;
        const VoxelCoord chunkOrigin{ chunkX * kVoxelChunkSize, 0, chunkZ * kVoxelChunkSize };
        for ( int32 y = 0; y < kVoxelChunkHeight; ++y )
        {
            for ( int32 localZ = 0; localZ < kVoxelChunkSize; ++localZ )
            {
                for ( int32 localX = 0; localX < kVoxelChunkSize; ++localX )
                {
                    const VoxelCoord      coord{ chunkOrigin._x + localX, y, chunkOrigin._z + localZ };
                    const VoxelBlockIndex blockIndex = world.getBlock( coord );
                    const VoxelBlockDef*  pBlock     = pCatalog->findBlock( blockIndex );
                    if ( pBlock == nullptr )
                        continue;
                    vector<VoxelMeshVertex>& outList = pBlock->_color._w < 0.999f ? outMesh._listTranslucentVertex : outMesh._listOpaqueVertex;
                    for ( int32 faceIndex = 0; faceIndex < kVoxelFaceCount; ++faceIndex )
                    {
                        const VoxelFace face = static_cast<VoxelFace>( faceIndex );
                        if ( VoxelMesherInternal::isFaceVisible( world, blockIndex, coord + getVoxelFaceOffset( face ) ) )
                            VoxelMesherInternal::pushFace( world, *pBlock, face, coord, chunkOrigin, outList );
                    }
                }
            }
        }
    }
} // namespace sw
