#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Client/VoxelMesher.h"

#include "Core/Container/array.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Profiling/FrameProfiler.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

namespace sw
{
    namespace
    {
        /** @brief 면 하나의 기하입니다. 원점 셀의 꼭짓점과 두 접선 축(u × v = 노멀)을 가집니다. 꼭짓점 순서 o, o+u, o+u+v, o+v 가 바깥에서 본 앞면 감김 순서입니다. */
        struct VoxelFaceFrame
        {
            VoxelCoord _origin;
            int32      _normalAxis; ///< 0 X, 1 Y, 2 Z
            int32      _axisU;      ///< 접선 u 의 축(양의 방향)
            int32      _axisV;      ///< 접선 v 의 축(양의 방향)
        };

        /** @brief 청크와 이웃 한 칸 테두리의 블록입니다. 지역 좌표 x · z 는 -1 ~ 16, y 는 -1 ~ 64 이고 월드 밖 · 올라오지 않은 청크는 공기입니다. */
        struct VoxelMesherVolume
        {
            static constexpr int32 kSizeXZ = kVoxelChunkSize + 2;
            static constexpr int32 kSizeY  = kVoxelChunkHeight + 2;

            array<VoxelBlockIndex, static_cast<size_t>( kSizeXZ* kSizeY* kSizeXZ )> _arrBlock;

            static constexpr int32 computeIndex( int32 localX, int32 y, int32 localZ ) { return ( ( y + 1 ) * kSizeXZ + ( localZ + 1 ) ) * kSizeXZ + ( localX + 1 ); }
            VoxelBlockIndex        get( const VoxelCoord& local ) const { return _arrBlock[static_cast<size_t>( computeIndex( local._x, local._y, local._z ) )]; }
        };

        /** @brief 블록 번호마다 미리 찾은 정의와 성질입니다. 메싱 안에서 카탈로그를 다시 찾지 않습니다. */
        struct VoxelMesherBlockTable
        {
            array<const VoxelBlockDef*, 256> _arrDef{};
            array<uint8, 256>                _arrOpaque{};
        };

        struct VoxelMesherInternal
        {
            static constexpr VoxelFaceFrame kArrFaceFrame[kVoxelFaceCount] = {
                {{ 1, 0, 0 }, 0, 1, 2}, // +X : Y × Z = X
                {{ 0, 0, 0 }, 0, 2, 1}, // -X : Z × Y = -X
                {{ 0, 1, 0 }, 1, 2, 0}, // +Y : Z × X = Y
                {{ 0, 0, 0 }, 1, 0, 2}, // -Y : X × Z = -Y
                {{ 0, 0, 1 }, 2, 0, 1}, // +Z : X × Y = Z
                {{ 0, 0, 0 }, 2, 1, 0}, // -Z : Y × X = -Z
            };
            static constexpr int32 kArrAxisSize[3] = { kVoxelChunkSize, kVoxelChunkHeight, kVoxelChunkSize };

            // 마스크 칸: 비트 31 있음, 비트 16 그늘이 고름(합칠 수 있음), 비트 8~15 꼭짓점 그늘 넷(2비트씩), 비트 0~7 블록 번호.
            static constexpr uint32 kMaskPresent      = 1u << 31;
            static constexpr uint32 kMaskUniform      = 1u << 16;
            static constexpr uint32 kByteMask         = 0xffu;
            static constexpr uint32 kOcclusionRepeat  = 0x55u;                 // 2비트 값 하나를 네 꼭짓점에 되풀이한다
            static constexpr uint32 kOcclusionAllOpen = 3u * kOcclusionRepeat; // 네 꼭짓점이 모두 트인 3
            static constexpr int32  kBorder           = 1;                     // 지역 배열이 청크 밖으로 읽는 테두리 폭

            static int32& axis( VoxelCoord& coord, int32 axisIndex ) { return axisIndex == 0 ? coord._x : ( axisIndex == 1 ? coord._y : coord._z ); }
            static int32  axis( const VoxelCoord& coord, int32 axisIndex ) { return axisIndex == 0 ? coord._x : ( axisIndex == 1 ? coord._y : coord._z ); }

            static VoxelCoord unit( int32 axisIndex, int32 length )
            {
                VoxelCoord coord{};
                axis( coord, axisIndex ) = length;
                return coord;
            }

            static float3 toFloat3( const VoxelCoord& coord )
            {
                return float3{ static_cast<float32>( coord._x ), static_cast<float32>( coord._y ), static_cast<float32>( coord._z ) };
            }

            static void fillVolume( const VoxelWorld& world, int32 chunkX, int32 chunkZ, VoxelMesherVolume& outVolume )
            {
                outVolume._arrBlock.fill( kVoxelAirBlock );
                for ( int32 localZ = -kBorder; localZ < kVoxelChunkSize + kBorder; ++localZ )
                {
                    for ( int32 localX = -kBorder; localX < kVoxelChunkSize + kBorder; ++localX )
                    {
                        // 이웃 청크 칸이면 그 청크의 기둥을 읽는다. 없는 청크(월드 밖 · 올라오지 않음)는 공기다.
                        const int32       neighborX = localX < 0 ? -kBorder : ( localX >= kVoxelChunkSize ? kBorder : 0 );
                        const int32       neighborZ = localZ < 0 ? -kBorder : ( localZ >= kVoxelChunkSize ? kBorder : 0 );
                        const VoxelChunk* pChunk    = world.findChunk( chunkX + neighborX, chunkZ + neighborZ );
                        if ( pChunk == nullptr )
                            continue;
                        const int32 sourceX = localX - neighborX * kVoxelChunkSize;
                        const int32 sourceZ = localZ - neighborZ * kVoxelChunkSize;
                        for ( int32 y = 0; y < kVoxelChunkHeight; ++y )
                        {
                            outVolume._arrBlock[static_cast<size_t>( VoxelMesherVolume::computeIndex( localX, y, localZ ) )] =
                                pChunk->_listBlock[static_cast<size_t>( ( y * kVoxelChunkSize + sourceZ ) * kVoxelChunkSize + sourceX )];
                        }
                    }
                }
            }

            static void fillBlockTable( const VoxelBlockCatalog& catalog, VoxelMesherBlockTable& outTable )
            {
                const vector<VoxelBlockDef>& listBlock = catalog.getBlocks();
                for ( size_t index = 0; index < listBlock.size() && index + 1 < outTable._arrDef.size(); ++index )
                {
                    outTable._arrDef[index + 1]    = &listBlock[index];
                    outTable._arrOpaque[index + 1] = listBlock[index]._bOpaque != SW_FALSE ? 1 : 0;
                }
            }

            /** @brief 면이 드러나는지 확인합니다. 이웃이 공기이면 드러나고, 불투명이면 가리고, 같은 반투명 블록(물 옆의 물)이면 가립니다. */
            static bool isFaceVisible( const VoxelMesherBlockTable& table, VoxelBlockIndex block, VoxelBlockIndex neighborBlock )
            {
                if ( neighborBlock == kVoxelAirBlock )
                    return true;
                if ( table._arrOpaque[neighborBlock] != 0 )
                    return false;
                return neighborBlock != block;
            }

            /** @brief 꼭짓점 그늘 단계(0~3)입니다. 옆 둘이 다 막히면 모서리와 상관없이 0입니다. */
            static int32 computeOcclusion( int32 side1, int32 side2, int32 corner )
            {
                if ( side1 != 0 && side2 != 0 )
                    return 0;
                return 3 - ( side1 + side2 + corner );
            }

            /** @brief 면 하나의 마스크 칸을 만듭니다. 불투명 블록만 꼭짓점 그늘을 셉니다(반투명은 모두 트인 3). */
            static uint32 makeMaskCell( const VoxelMesherVolume& volume, const VoxelMesherBlockTable& table, VoxelBlockIndex block, const VoxelFaceFrame& frame,
                                        const VoxelCoord& front )
            {
                // 꼭짓점 0~3은 (u, v) 의 (0,0), (1,0), (1,1), (0,1) 에 해당한다.
                constexpr int32 kArrCornerU[4] = { 0, 1, 1, 0 };
                constexpr int32 kArrCornerV[4] = { 0, 0, 1, 1 };
                uint32          occlusionBits  = kOcclusionAllOpen;
                if ( table._arrOpaque[block] != 0 )
                {
                    occlusionBits = 0;
                    for ( int32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
                    {
                        const VoxelCoord sideU = unit( frame._axisU, kArrCornerU[cornerIndex] * 2 - 1 );
                        const VoxelCoord sideV = unit( frame._axisV, kArrCornerV[cornerIndex] * 2 - 1 );
                        const int32      level = computeOcclusion( table._arrOpaque[volume.get( front + sideU )], table._arrOpaque[volume.get( front + sideV )],
                                                                   table._arrOpaque[volume.get( front + sideU + sideV )] );
                        occlusionBits |= static_cast<uint32>( level ) << ( cornerIndex * 2 );
                    }
                }
                const bool bUniform = occlusionBits == ( occlusionBits & 3u ) * kOcclusionRepeat;
                return kMaskPresent | ( bUniform ? kMaskUniform : 0u ) | ( occlusionBits << 8 ) | static_cast<uint32>( block );
            }

            /**
             * @brief 면 안 블록 좌표(u, v)입니다. 옆면은 위쪽이 v 0이어서 텍스처의 윗줄이 블록의 위쪽이 되고, 윗면과 아랫면은 X 가 u, Z 가 v 입니다.
             *        @p corner 는 사각형 원점 셀 기준 꼭짓점, @p extent 는 사각형이 덮는 축마다의 블록 수입니다(합친 면이면 1 보다 크다).
             */
            static float2 computeCornerLocalUV( VoxelFace face, const VoxelCoord& corner, const VoxelCoord& extent )
            {
                if ( face == VoxelFace::PositiveY || face == VoxelFace::NegativeY )
                    return float2{ static_cast<float32>( corner._x ), static_cast<float32>( corner._z ) };
                // 바깥에서 볼 때 u 가 오른쪽으로 늘어나게 한다. 글자와 무늬가 거울에 비친 것처럼 뒤집히지 않게 하려는 것이다.
                int32 u = 0;
                if ( face == VoxelFace::PositiveX )
                    u = extent._z - corner._z;
                else if ( face == VoxelFace::NegativeX )
                    u = corner._z;
                else if ( face == VoxelFace::PositiveZ )
                    u = corner._x;
                else
                    u = extent._x - corner._x;
                return float2{ static_cast<float32>( u ), static_cast<float32>( extent._y - corner._y ) };
            }

            /** @brief 사각형 하나(원점 셀 @p base 에서 u 축으로 @p width, v 축으로 @p height 블록)를 삼각형 둘로 냅니다. 좌표는 청크 지역 좌표입니다. */
            static void pushQuad( const VoxelBlockCatalog& catalog, const VoxelBlockDef& block, VoxelFace face, const VoxelCoord& base, int32 width, int32 height,
                                  uint32 occlusionBits, vector<VoxelMeshVertex>& outList )
            {
                const VoxelFaceFrame& frame     = kArrFaceFrame[static_cast<int32>( face )];
                const VoxelCoord      normal    = getVoxelFaceOffset( face );
                const VoxelCoord      extent    = VoxelCoord{ 1, 1, 1 } + unit( frame._axisU, width - 1 ) + unit( frame._axisV, height - 1 );
                const int32           tile      = block._arrFaceTile[static_cast<int32>( face )];
                const float2          tileBase  = float2{ static_cast<float32>( tile % catalog.getAtlasColumns() ) * VoxelMesher::kTileUVStride,
                                                static_cast<float32>( tile / catalog.getAtlasColumns() ) * VoxelMesher::kTileUVStride };
                const float32         faceShade = getFaceShadeValue( face );

                constexpr int32 kArrCornerU[4] = { 0, 1, 1, 0 };
                constexpr int32 kArrCornerV[4] = { 0, 0, 1, 1 };
                VoxelMeshVertex arrVertex[4];
                int32           arrOcclusion[4];
                for ( int32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
                {
                    arrOcclusion[cornerIndex]     = static_cast<int32>( ( occlusionBits >> ( cornerIndex * 2 ) ) & 3u );
                    const VoxelCoord cornerOffset = frame._origin + unit( frame._axisU, kArrCornerU[cornerIndex] * width ) + unit( frame._axisV, kArrCornerV[cornerIndex] * height );
                    const float32    shade        = faceShade * VoxelMesher::getOcclusionShade( arrOcclusion[cornerIndex] );
                    VoxelMeshVertex& vertex       = arrVertex[cornerIndex];
                    vertex._position              = toFloat3( base + cornerOffset );
                    vertex._normal                = toFloat3( normal );
                    vertex._uv                    = tileBase + computeCornerLocalUV( face, cornerOffset, extent );
                    vertex._color                 = float4{ block._color._x * shade, block._color._y * shade, block._color._z * shade, block._color._w };
                }
                // 그늘이 0-2 대각선 쪽으로 쏠렸으면 1-3 대각선으로 자른다. 순환 순서라 감김 방향은 그대로이다.
                const int32     firstCorner           = ( arrOcclusion[0] + arrOcclusion[2] < arrOcclusion[1] + arrOcclusion[3] ) ? 1 : 0;
                constexpr int32 kArrTriangleCorner[6] = { 0, 1, 2, 0, 2, 3 };
                for ( const int32 corner : kArrTriangleCorner )
                {
                    outList.push_back( arrVertex[( corner + firstCorner ) % 4] );
                }
            }

            /** @brief 한 방향의 면을 평면마다 마스크로 모아 합칩니다. 고른 그늘의 칸은 u 로 먼저 늘이고 v 로 줄째 늘입니다. */
            static void meshFaceDirection( const VoxelBlockCatalog& catalog, const VoxelMesherVolume& volume, const VoxelMesherBlockTable& table, VoxelFace face,
                                           VoxelChunkMesh& outMesh )
            {
                const VoxelFaceFrame&                                                     frame  = kArrFaceFrame[static_cast<int32>( face )];
                const VoxelCoord                                                          normal = getVoxelFaceOffset( face );
                const int32                                                               sizeU  = kArrAxisSize[frame._axisU];
                const int32                                                               sizeV  = kArrAxisSize[frame._axisV];
                array<uint32, static_cast<size_t>( kVoxelChunkSize * kVoxelChunkHeight )> arrMask;
                for ( int32 slice = 0; slice < kArrAxisSize[frame._normalAxis]; ++slice )
                {
                    bool bAnyFace = false;
                    for ( int32 v = 0; v < sizeV; ++v )
                    {
                        for ( int32 u = 0; u < sizeU; ++u )
                        {
                            const VoxelCoord      local    = unit( frame._normalAxis, slice ) + unit( frame._axisU, u ) + unit( frame._axisV, v );
                            const VoxelBlockIndex block    = volume.get( local );
                            uint32&               cell     = arrMask[static_cast<size_t>( v * sizeU + u )];
                            const bool            bVisible = table._arrDef[block] != nullptr && isFaceVisible( table, block, volume.get( local + normal ) );
                            cell                           = bVisible ? makeMaskCell( volume, table, block, frame, local + normal ) : 0u;
                            bAnyFace                       = bAnyFace || bVisible;
                        }
                    }
                    if ( bAnyFace == false )
                        continue;
                    for ( int32 v = 0; v < sizeV; ++v )
                    {
                        for ( int32 u = 0; u < sizeU; ++u )
                        {
                            const uint32 cell = arrMask[static_cast<size_t>( v * sizeU + u )];
                            if ( cell == 0u )
                                continue;
                            int32 width  = 1;
                            int32 height = 1;
                            if ( ( cell & kMaskUniform ) != 0u )
                            {
                                while ( u + width < sizeU && arrMask[static_cast<size_t>( v * sizeU + u + width )] == cell )
                                {
                                    ++width;
                                }
                                for ( bool bRowMatches = true; v + height < sizeV && bRowMatches; )
                                {
                                    for ( int32 step = 0; step < width && bRowMatches; ++step )
                                    {
                                        bRowMatches = arrMask[static_cast<size_t>( ( v + height ) * sizeU + u + step )] == cell;
                                    }
                                    if ( bRowMatches )
                                        ++height;
                                }
                            }
                            for ( int32 row = 0; row < height; ++row )
                            {
                                for ( int32 step = 0; step < width; ++step )
                                {
                                    arrMask[static_cast<size_t>( ( v + row ) * sizeU + u + step )] = 0u;
                                }
                            }
                            const VoxelBlockIndex    blockIndex = static_cast<VoxelBlockIndex>( cell & kByteMask );
                            const VoxelBlockDef&     block      = *table._arrDef[blockIndex];
                            vector<VoxelMeshVertex>& outList    = block._color._w < 0.999f ? outMesh._listTranslucentVertex : outMesh._listOpaqueVertex;
                            const VoxelCoord         base       = unit( frame._normalAxis, slice ) + unit( frame._axisU, u ) + unit( frame._axisV, v );
                            pushQuad( catalog, block, face, base, width, height, ( cell >> 8 ) & kByteMask, outList );
                        }
                    }
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

        static_assert( VoxelMesher::kTileUVStride > static_cast<float32>( kVoxelChunkHeight ), "the tile stride must exceed the longest merged face" );
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
        SW_PROFILE_SCOPE( "GT.VoxelMesher.fillChunkMesh" ); // 감사 K-12 전후 비교 — 청크 하나 메싱 시간
        outMesh.clear();
        const VoxelBlockCatalog* pCatalog = world.getCatalog();
        if ( pCatalog == nullptr || world.findChunk( chunkX, chunkZ ) == nullptr )
            return;
        VoxelMesherVolume volume;
        VoxelMesherInternal::fillVolume( world, chunkX, chunkZ, volume );
        VoxelMesherBlockTable table;
        VoxelMesherInternal::fillBlockTable( *pCatalog, table );
        for ( int32 faceIndex = 0; faceIndex < kVoxelFaceCount; ++faceIndex )
        {
            VoxelMesherInternal::meshFaceDirection( *pCatalog, volume, table, static_cast<VoxelFace>( faceIndex ), outMesh );
        }
        SW_PROFILE_COUNT( "GT.VoxelMesher.vertices", outMesh._listOpaqueVertex.size() + outMesh._listTranslucentVertex.size() );
    }
} // namespace sw
