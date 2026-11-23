/**
 * @file VoxelWorld.h
 * @brief 청크로 나눈 유한한 복셀 월드입니다. 보이는 범위의 청크만 사전에 올려 두고, 블록을 읽고 쓰며, 다시 메싱할 청크의 판 번호를 올립니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Runtime/World/Land/LandRegistry.h"

namespace sw
{
    class Archive;
    class VoxelWorld;

    /** @brief 청크 한 변(X, Z)의 블록 수입니다. */
    constexpr int32 kVoxelChunkSize = 16;
    /** @brief 월드(청크)의 높이(Y)입니다. 청크는 세로로 나누지 않습니다. */
    constexpr int32 kVoxelChunkHeight = 64;
    /** @brief 청크 하나의 블록 수입니다. */
    constexpr int32 kVoxelChunkVolume = kVoxelChunkSize * kVoxelChunkSize * kVoxelChunkHeight;

    /** @brief 올라온 청크 하나입니다. 블록은 `(y × size + z) × size + x` 인덱스에 저장합니다. */
    struct VoxelChunk
    {
        vector<VoxelBlockIndex> _listBlock{};
        uint32                  _revision{ 0 };         ///< 블록이나 이웃 면이 바뀔 때마다 새 번호다(월드 하나에서 되풀이하지 않는다). 뷰가 지은 때의 번호와 견주어 메시를 다시 만든다
        uint8                   _bModified{ SW_FALSE }; ///< 생성하거나 저장본에서 올린 뒤 블록이 바뀌었다 — 내릴 때 저장본에 적는다
    };

    /** @brief 저장본 청크의 같은 블록 구간입니다(지금 카탈로그 번호). 청크 부피가 uint16 안이다. */
    struct VoxelBlockRun
    {
        VoxelBlockIndex _block{ kVoxelAirBlock };
        uint16          _count{ 0 };
    };

    /** @brief 저장본이 없는 청크의 블록을 채우는 생성 훅입니다. @p outBlocks 는 `kVoxelChunkVolume` 칸 공기로 받아 채웁니다. 같은 입력이면 같은 블록이어야 합니다. */
    SW_DECLARE_DELEGATE( void, VoxelChunkGenerator, const VoxelWorld& world, int32 chunkX, int32 chunkZ, vector<VoxelBlockIndex>& outBlocks );
} // namespace sw

namespace sw
{
    /**
     * @class VoxelWorld
     * @brief `chunkCountX × chunkCountZ` 청크(높이 `kVoxelChunkHeight`)로 이루어진 월드입니다. 좌표 (0,0,0) 은 첫 청크의 아래쪽 모서리입니다.
     * @details 월드 밖은 공기로 읽히고 쓰기는 거절한다. **올라온(상주) 청크만** 블록을 가진다 — "청크 좌표 → 청크" 사전이다.
     *          올라오지 않은 청크는 공기로 읽히고 쓰기를 거절하므로, 몸 · 광선이 닿는 범위는 상주 반경 안이어야 한다(`updateResidency`).
     *          청크를 올릴 때 저장본이 있으면 그것을, 없으면 생성 훅(`setChunkGenerator`)이 채운다. 내릴 때는 바뀐 청크만 저장본(구간 압축)에 적고
     *          바뀌지 않은 청크는 버린다 — 생성이 결정적이라 다시 올리면 같다. Minecraft 류 청크 스트리밍(보이는 거리의 청크만 메모리, 바뀐 청크만 저장)과 같은 자리.
     *          블록이 바뀌면 그 청크의 판 번호(`VoxelChunk::_revision`)를 올린다. 청크 경계의 블록이면 이웃 청크도 올리는데, 이웃의 가려져 있던 면이 드러나기 때문이다.
     *          월드는 누가 그렸는지 모른다 — 뷰가 지은 때의 번호를 들고 견준다(전용 서버는 뷰가 없다).
     *          카탈로그는 빌려 쓴다.
     */
    class SW_KIT_API VoxelWorld
    {
    public:
        static constexpr uint32 kStateTag     = FourCCUtil::make( "VXWL" );
        static constexpr uint32 kStateVersion = 3; ///< 3: 바뀐 청크만 (청크 x, z, 구간) 으로 싣는다. 2: 모든 청크를 순서대로(판 올림 단계가 3 으로 옮긴다)

        VoxelWorld();

        /** @brief 크기를 정하고 올라온 청크 · 저장본을 비웁니다. 청크 수가 1 미만이면 오류를 남기고 false 입니다(조용히 1 로 맞추지 않습니다). */
        [[nodiscard]] bool initialize( int32 chunkCountX, int32 chunkCountZ, const VoxelBlockCatalog* pCatalog );
        /** @brief 저장본이 없는 청크를 채울 생성 훅을 정합니다. 훅 없이 저장본도 없는 청크는 올리지 않습니다(`loadChunk` 가 오류). */
        void setChunkGenerator( VoxelChunkGenerator generator ) { _generator = std::move( generator ); }
        /**
         * @brief 공유 땅을 빌립니다. 복셀 땅은 이 키트의 것이므로 월드 발자국(X × Z 블록, 막힘) 전체를 한 번에 얻습니다. 블록 (0, 0) 이 땅의 @p origin 셀에 해당합니다.
         * @return 그 위치에 다른 키트의 땅이 있으면 false 를 반환하고 아무것도 얻지 않습니다. @p pLand 가 nullptr 이면 true 를 반환합니다. `initialize` 뒤에 호출합니다.
         */
        [[nodiscard]] bool bindLand( LandRegistry* pLand, const int2& origin );

        /**
         * @brief 청크 하나를 올립니다. 저장본이 있으면 풀고, 없으면 생성 훅으로 채웁니다. 이웃 여덟 청크의 판 번호도 올립니다(경계 면 · 꼭짓점 그늘).
         * @return 이미 올라왔으면 true. 월드 밖이거나 저장본 · 생성 훅이 둘 다 없으면 false(뒤의 것은 오류를 남긴다).
         */
        [[nodiscard]] bool loadChunk( int32 chunkX, int32 chunkZ );
        /** @brief 청크 하나를 내립니다. 바뀐 청크이면 저장본에 적습니다. 올라오지 않았으면 아무것도 하지 않습니다. */
        void unloadChunk( int32 chunkX, int32 chunkZ );
        /**
         * @brief 가운데 청크에서 체비쇼프 거리 @p radius 안만 올라오게 합니다. 밖의 청크를 내리고, 안의 빠진 청크를 가까운 것부터 @p maxLoads 개까지 올립니다.
         * @return 이번에 올리거나 내린 청크 수입니다(0 이면 바뀐 것이 없다).
         */
        uint32 updateResidency( int32 centerChunkX, int32 centerChunkZ, int32 radius, int32 maxLoads );
        bool   isChunkResident( int32 chunkX, int32 chunkZ ) const { return findChunk( chunkX, chunkZ ) != nullptr; }
        uint32 getResidentChunkCount() const { return static_cast<uint32>( _mapChunk.size() ); }
        /** @brief 저장본(내린 뒤에도 남는 바뀐 청크)의 수입니다. */
        uint32 getStoredChunkCount() const { return static_cast<uint32>( _mapStoredChunk.size() ); }
        /** @brief 청크 좌표 → 사전 키입니다(x 가 높은 32비트). 게임이 청크마다 뷰를 둘 때도 같은 키를 쓴다. */
        static constexpr uint64 makeChunkKey( int32 chunkX, int32 chunkZ )
        {
            return ( static_cast<uint64>( static_cast<uint32>( chunkX ) ) << 32 ) | static_cast<uint64>( static_cast<uint32>( chunkZ ) );
        }
        /** @brief `makeChunkKey` 의 거꿀입니다. */
        static constexpr int32 toChunkX( uint64 key ) { return static_cast<int32>( static_cast<uint32>( key >> 32 ) ); }
        static constexpr int32 toChunkZ( uint64 key ) { return static_cast<int32>( static_cast<uint32>( key ) ); }
        /** @brief 블록 좌표 → 그 블록이 든 청크 좌표(내림 나눗셈 — 음수도 바르다)입니다. */
        static constexpr int32 toChunkCoord( int32 blockCoord ) { return blockCoord >= 0 ? blockCoord / kVoxelChunkSize : ( blockCoord + 1 ) / kVoxelChunkSize - 1; }

        /** @brief 블록 번호를 반환합니다. 월드 밖 · 올라오지 않은 청크는 공기입니다. */
        VoxelBlockIndex getBlock( int32 x, int32 y, int32 z ) const;
        VoxelBlockIndex getBlock( const VoxelCoord& coord ) const { return getBlock( coord._x, coord._y, coord._z ); }
        /** @brief 블록을 씁니다. 월드 밖이거나 청크가 올라오지 않았으면 false 를 반환합니다. 같은 값이면 판 번호를 올리지 않고 true 를 반환합니다. */
        [[nodiscard]] bool setBlock( int32 x, int32 y, int32 z, VoxelBlockIndex block );
        [[nodiscard]] bool setBlock( const VoxelCoord& coord, VoxelBlockIndex block ) { return setBlock( coord._x, coord._y, coord._z, block ); }
        /** @brief 몸이 통과하지 못하는 블록이면 true 입니다. 월드 아래(y < 0)는 막혀 있고, 옆쪽과 위쪽 밖은 비어 있습니다. */
        bool isSolid( int32 x, int32 y, int32 z ) const;
        bool isOpaque( int32 x, int32 y, int32 z ) const;
        bool isInside( int32 x, int32 y, int32 z ) const;
        /** @brief 그 기둥에서 가장 높은 단단한 블록의 y 를 반환합니다. 없으면 비어 있습니다. */
        optional<int32> findTopSolidY( int32 x, int32 z ) const;

        /** @brief 올라온 청크 하나입니다. 월드 밖 · 올라오지 않았으면 nullptr 입니다. 청크를 올리거나 내리면 주소가 바뀔 수 있습니다. */
        const VoxelChunk* findChunk( int32 chunkX, int32 chunkZ ) const;

        const VoxelBlockCatalog* getCatalog() const { return _pCatalog; }
        int32                    getChunkCountX() const { return _chunkCountX; }
        int32                    getChunkCountZ() const { return _chunkCountZ; }
        int32                    getSizeX() const { return _chunkCountX * kVoxelChunkSize; }
        int32                    getSizeY() const { return kVoxelChunkHeight; }
        int32                    getSizeZ() const { return _chunkCountZ * kVoxelChunkSize; }
        /** @brief 올라온 청크에서 번호가 @p block 인 블록 수를 반환합니다. 테스트와 디버그에 씁니다. */
        uint32 countBlocks( VoxelBlockIndex block ) const;

        /**
         * @brief 블록 id 팔레트(세이브 번호 → id)를 한 번 쓰고, 바뀐 청크(저장본 + 올라와서 바뀐 청크)마다 청크 좌표와 같은 블록이 이어지는 구간(팔레트 번호와 길이)을 씁니다.
         *        바뀌지 않은 청크는 싣지 않는다 — 읽는 쪽이 같은 생성 훅으로 다시 만든다. 핫 리로드와 세이브에 씁니다. 청크는 좌표 순서로 써서 같은 월드면 같은 바이트다.
         *        읽으면 실린 청크가 저장본이 되고, 올라와 있던 청크는 저장본(없으면 생성 훅)으로 다시 채워 판 번호를 올립니다. 같은 청크 수로 `initialize` 한 뒤에 부릅니다.
         *        블록은 id 로 지금 카탈로그 번호에 다시 짝지어지므로 `blocks.xml` 순서가 바뀌어도 같은 블록이 나옵니다.
         *        @return 옛 판을 올릴 수 없거나, 줄어든 월드 밖에 블록이 남은 청크가 있거나, 지금 카탈로그에 없는 블록 id 나 팔레트 밖 번호가 있거나, 바이트가 손상됐으면 false 를 반환하고 상태는 그대로 둡니다.
         */
        SW_DECLARE_STATE_IO_IN_PLACE();

    private:
        bool        isChunkInside( int32 chunkX, int32 chunkZ ) const { return chunkX >= 0 && chunkZ >= 0 && chunkX < _chunkCountX && chunkZ < _chunkCountZ; }
        VoxelChunk* findChunkMutable( int32 chunkX, int32 chunkZ );
        /** @brief 올라온 청크에 새 판 번호를 줍니다(올라오지 않았으면 할 일이 없다). */
        void advanceChunkRevision( int32 chunkX, int32 chunkZ );
        /** @brief 생성 훅으로 @p outBlocks 를 채웁니다. 훅이 없으면 오류를 남기고 공기로 둔 채 false 입니다. */
        [[nodiscard]] bool fillGeneratedBlocks( int32 chunkX, int32 chunkZ, vector<VoxelBlockIndex>& outBlocks ) const;

    private:
        unordered_map<uint64, VoxelChunk>            _mapChunk;       ///< 올라온 청크 — 키는 `makeChunkKey`
        unordered_map<uint64, vector<VoxelBlockRun>> _mapStoredChunk; ///< 바뀐 청크의 저장본(구간 압축) — 내린 뒤에도 남고 세이브에 실린다
        VoxelChunkGenerator                          _generator;
        const VoxelBlockCatalog*                     _pCatalog;
        LandBinding                                  _land; ///< 빌린 공유 땅이다. 없으면 단독으로 쓴다
        int32                                        _chunkCountX;
        int32                                        _chunkCountZ;
        uint32                                       _lastRevision; ///< 마지막으로 준 청크 판 번호. 월드를 다시 열어도(`initialize`) 이어 센다 — 앞 판에서 지은 뷰가 새 청크를 같은 판으로 보지 않게
    };
} // namespace sw
