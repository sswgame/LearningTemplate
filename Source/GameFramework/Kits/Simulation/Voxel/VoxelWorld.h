/**
 * @file VoxelWorld.h
 * @brief 청크로 나눈 유한 복셀 월드 — 블록 읽기 · 쓰기와 다시 메싱할 청크 표시입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelBlock.h"

namespace sw
{
    /** @brief 청크 한 변(X · Z)의 블록 수입니다. */
    constexpr int32 kVoxelChunkSize = 16;
    /** @brief 월드(청크)의 높이(Y)입니다. 청크는 세로로 나누지 않습니다. */
    constexpr int32 kVoxelChunkHeight = 64;
    /** @brief 청크 하나의 블록 수입니다. */
    constexpr int32 kVoxelChunkVolume = kVoxelChunkSize * kVoxelChunkSize * kVoxelChunkHeight;

    /** @brief 청크 하나입니다. 블록은 `(y × size + z) × size + x` 자리입니다. */
    struct VoxelChunk
    {
        vector<VoxelBlockIndex> _listBlock{};
        uint32                  _revision{ 0 }; ///< 블록이 바뀔 때마다 오른다(게임이 메시를 다시 지을지 본다)
        uint8                   _bDirty{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class VoxelWorld
     * @brief `chunkCountX × chunkCountZ` 청크(높이 `kVoxelChunkHeight`)의 월드입니다. 좌표 (0,0,0) 이 첫 청크의 아래 구석입니다.
     * @details 월드 밖은 공기로 읽힙니다(쓰기는 거절). 블록이 바뀌면 그 청크와 — 청크 경계 블록이면 — 이웃 청크가 다시 메싱할 것으로 표시됩니다
     *          (이웃의 가려진 면이 드러나기 때문). 카탈로그는 빌려 씁니다.
     */
    class SW_GF_API VoxelWorld
    {
    public:
        VoxelWorld();

        /** @brief 크기를 정하고 모두 공기로 채웁니다. */
        void initialize( int32 chunkCountX, int32 chunkCountZ, const VoxelBlockCatalog* pCatalog );

        /** @brief 블록 번호입니다. 월드 밖은 공기입니다. */
        VoxelBlockIndex getBlock( int32 x, int32 y, int32 z ) const;
        VoxelBlockIndex getBlock( const VoxelCoord& coord ) const { return getBlock( coord._x, coord._y, coord._z ); }
        /** @brief 블록을 씁니다. 월드 밖이면 false 입니다. 같은 값이면 아무것도 표시하지 않고 true 입니다. */
        [[nodiscard]] bool setBlock( int32 x, int32 y, int32 z, VoxelBlockIndex block );
        [[nodiscard]] bool setBlock( const VoxelCoord& coord, VoxelBlockIndex block ) { return setBlock( coord._x, coord._y, coord._z, block ); }
        /** @brief 몸이 통과하지 못하는 블록이면 true 입니다. 월드 아래(y < 0)는 막혀 있고 옆 · 위 밖은 비어 있습니다. */
        bool isSolid( int32 x, int32 y, int32 z ) const;
        bool isOpaque( int32 x, int32 y, int32 z ) const;
        bool isInside( int32 x, int32 y, int32 z ) const;
        /** @brief 그 기둥에서 가장 높은 단단한 블록의 y 입니다. 없으면 -1 입니다. */
        int32 findTopSolidY( int32 x, int32 z ) const;

        /** @brief 다시 메싱할 청크면 true 입니다. */
        bool isChunkDirty( int32 chunkX, int32 chunkZ ) const;
        void clearChunkDirty( int32 chunkX, int32 chunkZ );
        /** @brief 모든 청크를 다시 메싱할 것으로 표시합니다(지형 생성 뒤). */
        void              markAllChunksDirty();
        const VoxelChunk* findChunk( int32 chunkX, int32 chunkZ ) const;

        const VoxelBlockCatalog* getCatalog() const { return _pCatalog; }
        int32                    getChunkCountX() const { return _chunkCountX; }
        int32                    getChunkCountZ() const { return _chunkCountZ; }
        int32                    getSizeX() const { return _chunkCountX * kVoxelChunkSize; }
        int32                    getSizeY() const { return kVoxelChunkHeight; }
        int32                    getSizeZ() const { return _chunkCountZ * kVoxelChunkSize; }
        /** @brief 번호가 @p block 인 블록 수입니다(시험 · 디버그). */
        uint32 countBlocks( VoxelBlockIndex block ) const;

    private:
        VoxelChunk* findChunkMutable( int32 chunkX, int32 chunkZ );
        void        markChunkDirty( int32 chunkX, int32 chunkZ );

        vector<VoxelChunk>       _listChunk; ///< `chunkZ × countX + chunkX`
        const VoxelBlockCatalog* _pCatalog;
        int32                    _chunkCountX;
        int32                    _chunkCountZ;
    };
} // namespace sw
