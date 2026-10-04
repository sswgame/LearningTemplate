/**
 * @file GridReachability.h
 * @brief 칸 이동 범위 — 이동력 안에서 갈 수 있는 칸(지형 비용 · 막힘 · 아군은 지나가되 서지 못함), 그 칸까지의 길, 공격 사거리 칸입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/GridTopology.h"

#include <algorithm>

namespace sw
{
    /**
     * @class GridReachability
     * @brief SRPG(파이어 엠블렘 · G 제네레이션 · 택틱스) 의 "파란 칸" 입니다. 이동 비용은 부르는 쪽이 함수로 줍니다 — 유닛 종류(지상 · 공중 · 수중)마다 지형 값이 달라서입니다.
     * @code
     *     reach.compute( width, height, unitCell, movePoints,
     *         [&]( const int2& from, const int2& to ) { return isEnemyAt( to ) ? -1 : terrainCost( to, moveType ); },
     *         [&]( const int2& cell ) { return isOccupied( cell ) == false; } );
     * @endcode
     * @details 다익스트라입니다(비용은 작은 정수). 결과는 다음 `compute` 까지 남습니다.
     */
    class SW_GF_API GridReachability
    {
    public:
        static constexpr int32 kUnreached = -1;

        /**
         * @param stepCost `int32( const int2& from, const int2& to )` — 들어가는 비용, 음수면 못 들어간다
         * @param canStop  `bool( const int2& cell )` — 설 수 있는가(아군 칸은 지나가기만)
         */
        template <typename TStepCost, typename TCanStop>
        void compute( int32 width, int32 height, const int2& start, int32 movePoints, TStepCost&& stepCost, TCanStop&& canStop, bool bDiagonal = false )
        {
            _width                 = width > 0 ? width : 0;
            _height                = height > 0 ? height : 0;
            const size_t cellCount = static_cast<size_t>( _width * _height );
            _listCost.assign( cellCount, kUnreached );
            _listParent.assign( cellCount, -1 );
            _listStoppable.assign( cellCount, SW_FALSE );
            _listOpen.clear();
            if ( isInside( start ) == false )
                return;
            const int32 startIndex                            = computeIndex( start );
            _listCost[static_cast<size_t>( startIndex )]      = 0;
            _listStoppable[static_cast<size_t>( startIndex )] = SW_TRUE; // 제자리는 늘 선다
            pushOpen( 0, startIndex );
            const int32 directionCount = bDiagonal ? GridTopology::kNeighborCount : GridTopology::kOrthogonalCount;
            while ( _listOpen.empty() == false )
            {
                std::pop_heap( _listOpen.begin(), _listOpen.end(), &GridReachability::isWorse );
                const OpenEntry entry = _listOpen.back();
                _listOpen.pop_back();
                if ( entry._cost != _listCost[static_cast<size_t>( entry._index )] )
                    continue; // 더 싼 길로 이미 펼쳤다
                const int2 cell{ entry._index % _width, entry._index / _width };
                for ( int32 direction = 0; direction < directionCount; ++direction )
                {
                    const int2 next = GridTopology::getNeighbor( cell, direction );
                    if ( isInside( next ) == false )
                        continue;
                    const int32 step = stepCost( cell, next );
                    if ( step < 0 )
                        continue;
                    const int32 newCost   = entry._cost + step;
                    const int32 nextIndex = computeIndex( next );
                    const int32 oldCost   = _listCost[static_cast<size_t>( nextIndex )];
                    if ( newCost > movePoints || ( oldCost != kUnreached && oldCost <= newCost ) )
                        continue;
                    _listCost[static_cast<size_t>( nextIndex )]      = newCost;
                    _listParent[static_cast<size_t>( nextIndex )]    = entry._index;
                    _listStoppable[static_cast<size_t>( nextIndex )] = canStop( next ) ? SW_TRUE : SW_FALSE;
                    pushOpen( newCost, nextIndex );
                }
            }
        }

        /** @brief 이동해서 설 수 있는 칸인가입니다. */
        bool isReachable( const int2& cell ) const;
        /** @brief 그 칸까지의 비용입니다(지나갈 수만 있는 칸 포함). 못 가면 `kUnreached` 입니다. */
        int32 getCost( const int2& cell ) const;
        /** @brief 설 수 있는 칸들입니다(시작 칸 포함). */
        void collectReachable( vector<int2>& outListCell ) const;
        /** @brief 시작 → @p cell 의 칸 목록(시작 포함)입니다. 갈 수 없으면 false 입니다. */
        [[nodiscard]] bool makePath( const int2& cell, vector<int2>& outListCell ) const;
        /** @brief 설 수 있는 칸에서 사거리(맨해튼 @p minRange..@p maxRange) 안에 드는 칸 — "빨간 칸" 입니다(서는 칸과 겹쳐도 넣는다). */
        void collectAttackCells( int32 minRange, int32 maxRange, vector<int2>& outListCell ) const;
        /** @brief 한 칸에서 사거리 안의 칸들입니다(격자 밖은 뺀다). */
        static void collectRangeCells( const int2& center, int32 minRange, int32 maxRange, int32 width, int32 height, vector<int2>& outListCell );

    private:
        struct OpenEntry
        {
            int32 _cost;
            int32 _index;
        };

        static bool isWorse( const OpenEntry& lhs, const OpenEntry& rhs ) { return lhs._cost > rhs._cost; }

        bool  isInside( const int2& cell ) const { return cell._x >= 0 && cell._y >= 0 && cell._x < _width && cell._y < _height; }
        int32 computeIndex( const int2& cell ) const { return cell._y * _width + cell._x; }
        void  pushOpen( int32 cost, int32 index )
        {
            _listOpen.push_back( OpenEntry{ cost, index } );
            std::push_heap( _listOpen.begin(), _listOpen.end(), &GridReachability::isWorse );
        }

        vector<int32>             _listCost{};
        vector<int32>             _listParent{};
        vector<uint8>             _listStoppable{};
        vector<OpenEntry>         _listOpen{};
        mutable GridSearchScratch _cellMarks{}; ///< `collectAttackCells` 의 "한 칸 한 번" 표시
        int32                     _width{ 0 };
        int32                     _height{ 0 };
    };
} // namespace sw
