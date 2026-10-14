/**
 * @file VoxelWorld.h
 * @brief 청크로 나눈 유한한 복셀 월드입니다. 블록을 읽고 쓰며, 다시 메싱할 청크를 표시합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/World/Land/LandRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

namespace sw
{
    class Archive;

    /** @brief 청크 한 변(X, Z)의 블록 수입니다. */
    constexpr int32 kVoxelChunkSize = 16;
    /** @brief 월드(청크)의 높이(Y)입니다. 청크는 세로로 나누지 않습니다. */
    constexpr int32 kVoxelChunkHeight = 64;
    /** @brief 청크 하나의 블록 수입니다. */
    constexpr int32 kVoxelChunkVolume = kVoxelChunkSize * kVoxelChunkSize * kVoxelChunkHeight;

    /** @brief 청크 하나입니다. 블록은 `(y × size + z) × size + x` 인덱스에 저장합니다. */
    struct VoxelChunk
    {
        vector<VoxelBlockIndex> _listBlock{};
        uint32                  _revision{ 0 }; ///< 블록이 바뀔 때마다 올라간다. 게임이 이 값으로 메시를 다시 만들지 판단한다
        uint8                   _bDirty{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class VoxelWorld
     * @brief `chunkCountX × chunkCountZ` 청크(높이 `kVoxelChunkHeight`)로 이루어진 월드입니다. 좌표 (0,0,0) 은 첫 청크의 아래쪽 모서리입니다.
     * @details 월드 밖은 공기로 읽히고 쓰기는 거절한다.
     *          블록이 바뀌면 그 청크를 다시 메싱할 것으로 표시한다. 청크 경계의 블록이면 이웃 청크도 표시하는데, 이웃의 가려져 있던 면이 드러나기 때문이다.
     *          카탈로그는 빌려 쓴다.
     */
    class SW_KIT_API VoxelWorld
    {
    public:
        VoxelWorld();

        /** @brief 크기를 정하고 모두 공기로 채웁니다. */
        void initialize( int32 chunkCountX, int32 chunkCountZ, const VoxelBlockCatalog* pCatalog );
        /**
         * @brief 공유 땅을 빌립니다. 복셀 땅은 이 키트의 것이므로 월드 발자국(X × Z 블록, 막힘) 전체를 한 번에 얻습니다. 블록 (0, 0) 이 땅의 @p origin 셀에 해당합니다.
         * @return 그 위치에 다른 키트의 땅이 있으면 false 를 반환하고 아무것도 얻지 않습니다. @p pLand 가 nullptr 이면 true 를 반환합니다. `initialize` 뒤에 호출합니다.
         */
        [[nodiscard]] bool bindLand( LandRegistry* pLand, const int2& origin );

        /** @brief 블록 번호를 반환합니다. 월드 밖은 공기입니다. */
        VoxelBlockIndex getBlock( int32 x, int32 y, int32 z ) const;
        VoxelBlockIndex getBlock( const VoxelCoord& coord ) const { return getBlock( coord._x, coord._y, coord._z ); }
        /** @brief 블록을 씁니다. 월드 밖이면 false 를 반환합니다. 같은 값이면 아무것도 표시하지 않고 true 를 반환합니다. */
        [[nodiscard]] bool setBlock( int32 x, int32 y, int32 z, VoxelBlockIndex block );
        [[nodiscard]] bool setBlock( const VoxelCoord& coord, VoxelBlockIndex block ) { return setBlock( coord._x, coord._y, coord._z, block ); }
        /** @brief 몸이 통과하지 못하는 블록이면 true 입니다. 월드 아래(y < 0)는 막혀 있고, 옆쪽과 위쪽 밖은 비어 있습니다. */
        bool isSolid( int32 x, int32 y, int32 z ) const;
        bool isOpaque( int32 x, int32 y, int32 z ) const;
        bool isInside( int32 x, int32 y, int32 z ) const;
        /** @brief 그 기둥에서 가장 높은 단단한 블록의 y 를 반환합니다. 없으면 -1 입니다. */
        int32 findTopSolidY( int32 x, int32 z ) const;

        /** @brief 다시 메싱할 청크이면 true 입니다. */
        bool isChunkDirty( int32 chunkX, int32 chunkZ ) const;
        void clearChunkDirty( int32 chunkX, int32 chunkZ );
        /** @brief 모든 청크를 다시 메싱할 것으로 표시합니다. 지형을 생성한 뒤에 부릅니다. */
        void              markAllChunksDirty();
        const VoxelChunk* findChunk( int32 chunkX, int32 chunkZ ) const;

        const VoxelBlockCatalog* getCatalog() const { return _pCatalog; }
        int32                    getChunkCountX() const { return _chunkCountX; }
        int32                    getChunkCountZ() const { return _chunkCountZ; }
        int32                    getSizeX() const { return _chunkCountX * kVoxelChunkSize; }
        int32                    getSizeY() const { return kVoxelChunkHeight; }
        int32                    getSizeZ() const { return _chunkCountZ * kVoxelChunkSize; }
        /** @brief 번호가 @p block 인 블록 수를 반환합니다. 테스트와 디버그에 씁니다. */
        uint32 countBlocks( VoxelBlockIndex block ) const;

        /** @brief 청크마다 같은 블록이 이어지는 구간(번호와 길이)으로 블록을 씁니다. 핫 리로드와 세이브에 씁니다. 지형이 대부분 공기와 돌이라 결과가 작습니다. */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 가 쓴 바이트로 블록을 복원하고 모든 청크를 다시 메싱할 것으로 표시합니다. 같은 청크 수와 카탈로그로 `initialize` 한 뒤에 부릅니다.
         * @return 청크 수가 다르거나, 카탈로그에 없는 블록 번호가 있거나, 바이트가 손상됐으면 false 를 반환하고 상태는 그대로 둡니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        VoxelChunk* findChunkMutable( int32 chunkX, int32 chunkZ );
        void        markChunkDirty( int32 chunkX, int32 chunkZ );

        vector<VoxelChunk>       _listChunk; ///< 인덱스는 `chunkZ × countX + chunkX` 이다
        const VoxelBlockCatalog* _pCatalog;
        LandBinding              _land; ///< 빌린 공유 땅이다. 없으면 단독으로 쓴다
        int32                    _chunkCountX;
        int32                    _chunkCountZ;
    };
} // namespace sw
