#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Spatial/SpatialTree.h"

namespace sw
{
    /**
     * @brief 대규모 2D/2.5D 객체를 위한 고속 O(1) 공간 분할 해시 그리드
     */
    class SW_API SpatialHashGrid2D
    {
    public:
        static constexpr uint32 kCellKeyShift = 32;

        /**
         * @brief 핸들 하나를 그리드에 흩뿌릴 수 있는 최대 셀 수입니다.
         * @details 셀 범위는 **입력 좌표에서** 나온다 — 큰 AABB 하나가 수천만 개의 셀을 요구할 수
         *          있고, 이 모듈이 스스로 제공하는 `AABB2D::infinite()` 가 바로 그런 값이다. 상한을
         *          넘는 핸들은 셀에 흩뿌리는 대신 목록 하나에 모아 두고, 그리드를 보는 질의가 그
         *          목록을 **항상 함께** 본다. 그런 핸들은 수가 적고 어차피 거의 모든 질의에 걸리므로
         *          셀에 흩어 두는 것이 이득이 되지 않는다.
         * @note `PhysicsWorld::kMaxBodyCellCount` 와 값이 같지만 다른 질문이다(그쪽은 물리 바디,
         *       이쪽은 범용 질의 색인). 한쪽 사정으로 값을 바꿀 수 있어야 하므로 별칭을 두지 않는다.
         */
        static constexpr int64 kMaxHandleCellCount = 1024;

        /**
         * @brief 질의 하나가 훑을 수 있는 최대 셀 수입니다.
         * @details 넘으면 셀을 도는 대신 등록된 핸들 전부를 훑는다 — 셀 수가 핸들 수보다 많아지는
         *          순간부터 그리드를 도는 것은 순수한 손해다. `queryRay` 가 이미 같은 이유로
         *          걸음 수를 막고 있었는데(`kMaxRayStep`), 나머지 셋은 막혀 있지 않았다.
         */
        static constexpr int64 kMaxQueryCellCount = 4096;

        explicit SpatialHashGrid2D( float32 cellSize = constant::kDefaultSpatialCellSize );
        ~SpatialHashGrid2D()                                         = default;
        SpatialHashGrid2D( const SpatialHashGrid2D& )                = default;
        SpatialHashGrid2D& operator=( const SpatialHashGrid2D& )     = default;
        SpatialHashGrid2D( SpatialHashGrid2D&& ) noexcept            = default;
        SpatialHashGrid2D& operator=( SpatialHashGrid2D&& ) noexcept = default;

        void insert( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY );
        void update( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY );
        void remove( ObjectHandle handle );
        void clear();

        void queryAabb( float32 minX, float32 minY, float32 maxX, float32 maxY, vector<ObjectHandle>& outListHandle ) const;
        void queryCircle( float32 centerX, float32 centerY, float32 radius, vector<ObjectHandle>& outListHandle ) const;
        void queryRay( float32 startX, float32 startY, float32 dirX, float32 dirY, float32 maxDist, vector<ObjectHandle>& outListHandle ) const;

        float32 getCellSize() const { return _cellSize; }
        size_t  getHandleCount() const { return _mapHandleBound.size(); }
        size_t  getActiveBucketCount() const { return _mapBucket.size(); }

    private:
        /**
         * @struct CellRange
         * @brief 경계 상자 하나가 덮는 셀 범위 — **삽입·제거·질의가 같은 집합을 보게 하는 자리**입니다.
         * @details 이 계산이 네 군데에 복사돼 있었다(삽입 · 제거 · `queryAabb` · `queryCircle`).
         *          삽입이 덜 훑으면 그 핸들을 **질의가 못 찾고**, 제거가 덜 훑으면 죽은 핸들이 셀에
         *          남아 표가 끝없이 자란다 — 둘 다 그 자리에서 터지지 않는다.
         */
        struct CellRange
        {
            int32 _minX{ 0 };
            int32 _minY{ 0 };
            int32 _maxX{ -1 }; /**< 기본값은 비어 있는 범위다 (max < min). */
            int32 _maxY{ -1 };

            /** @brief 경계 상자가 덮는 셀 범위입니다. 뒤집힌 상자도 정규화해서 받습니다. */
            static CellRange fromBounds( float32 minX, float32 minY, float32 maxX, float32 maxY, float32 cellSize );

            /** @brief 이 범위가 덮는 셀 수입니다. 비어 있으면 0 입니다. */
            int64 getCellCount() const noexcept;

            /** @brief 범위의 모든 셀에 대해 실행합니다. 비어 있으면 한 번도 부르지 않습니다. */
            template <typename Func>
            void forEachCell( Func&& func ) const
            {
                for ( int32 cellY = _minY; cellY <= _maxY; ++cellY )
                {
                    for ( int32 cellX = _minX; cellX <= _maxX; ++cellX )
                    {
                        func( cellX, cellY );
                    }
                }
            }
        };

        /** @brief 셀을 훑기보다 등록된 핸들 전부를 도는 편이 나은가 — 범위가 비었거나 너무 넓으면 그렇습니다. */
        bool shouldScanAllHandles( const CellRange& range ) const;

        /**
         * @brief 범위에 걸린 후보 핸들마다 실행합니다. 중복은 걸러지지 않습니다.
         * @details 세 질의가 공유하는 자리다 — "어디를 보는가" 는 여기 하나에만 적혀 있고, 각 질의는
         *          "무엇을 남기는가"(좁은 판정)만 스스로 한다. 결과의 중복은 호출부가 마지막에
         *          정렬·유일화로 지운다.
         */
        template <typename Func>
        void forEachCandidateHandle( const CellRange& range, Func&& func ) const
        {
            if ( shouldScanAllHandles( range ) )
            {
                // 전부 도는 길에는 넘친 핸들도 이미 들어 있다(`_mapHandleBound` 는 모두를 든다).
                for ( const auto& pair : _mapHandleBound )
                    func( pair.first );
                return;
            }

            // 그리드에 흩뿌리기엔 너무 큰 핸들은 어느 셀에도 없다 — **항상 함께** 본다.
            for ( const ObjectHandle handle : _listOversizedHandle )
                func( handle );

            range.forEachCell( [this, &func]( int32 cellX, int32 cellY )
            {
                const auto bucketIt = _mapBucket.find( getCellKey( cellX, cellY ) );
                if ( bucketIt == _mapBucket.end() )
                    return;
                for ( const ObjectHandle handle : bucketIt->second )
                    func( handle );
            } );
        }

        uint64 getCellKey( int32 cellX, int32 cellY ) const
        {
            return ( static_cast<uint64>( static_cast<uint32>( cellX ) ) << kCellKeyShift ) |
                   ( static_cast<uint64>( static_cast<uint32>( cellY ) ) );
        }

        float32                                     _cellSize;
        unordered_map<uint64, vector<ObjectHandle>> _mapBucket;
        unordered_map<ObjectHandle, AABB2D>         _mapHandleBound;
        /** @brief 그리드에 흩뿌리기엔 너무 큰 핸들들. 그리드를 보는 질의가 **항상 함께** 봅니다. */
        vector<ObjectHandle> _listOversizedHandle;
    };
} // namespace sw
