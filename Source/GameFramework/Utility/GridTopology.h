/**
 * @file GridTopology.h
 * @brief 2D 칸 격자의 모양(칸 번호 · 경계 · 이웃 순서)과 너비 우선 탐색 · 표시용 재사용 스크래치입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

namespace sw
{
    /**
     * @struct GridTopology
     * @brief 너비 × 높이 칸 격자의 칸 번호(`y × 너비 + x`) · 경계 · 이웃 순서입니다.
     * @details 이웃 순서는 하나로 고정입니다 — 앞 넷이 직교(+x, −x, +y, −y), 뒤 넷이 대각선(+x+y, +x−y, −x+y, −x−y). 탐색 결과(경로 ·
     *          흐름장 · 번짐 알림 순서)가 이 순서에 매이므로 바꾸면 결정적 시뮬레이션의 결과가 바뀝니다. 내비 · 기믹 · 키트가 같은 표를 씁니다.
     */
    struct GridTopology
    {
        static constexpr int32 kOrthogonalCount = 4;
        static constexpr int32 kNeighborCount   = 8;
        static constexpr int32 kArrOffsetX[kNeighborCount]{ 1, -1, 0, 0, 1, 1, -1, -1 };
        static constexpr int32 kArrOffsetY[kNeighborCount]{ 0, 0, 1, -1, 1, -1, 1, -1 };

        int32 _width{ 0 };
        int32 _height{ 0 };

        constexpr GridTopology() = default;
        constexpr GridTopology( int32 width, int32 height )
            : _width{ width > 0 ? width : 0 }
            , _height{ height > 0 ? height : 0 }
        {
        }

        /** @brief @p cell 의 @p direction 번째 이웃입니다(경계는 보지 않는다). */
        static constexpr int2 getNeighbor( const int2& cell, int32 direction ) { return int2{ cell._x + kArrOffsetX[direction], cell._y + kArrOffsetY[direction] }; }

        constexpr bool  isInside( int32 x, int32 y ) const { return 0 <= x && x < _width && 0 <= y && y < _height; }
        constexpr bool  isInside( const int2& cell ) const { return isInside( cell._x, cell._y ); }
        constexpr int32 toIndex( const int2& cell ) const { return cell._y * _width + cell._x; }
        constexpr int2  toCell( int32 index ) const { return int2{ index % _width, index / _width }; }
        constexpr int32 getCellCount() const { return _width * _height; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class GridSearchScratch
     * @brief 칸 번호 위의 너비 우선 탐색 · "한 번만" 표시에 쓰는 스크래치입니다. 호출마다 W × H 를 새로 잡거나 지우지 않습니다.
     * @details `begin` 이 세대 번호만 올려 지난 표시를 한 번에 무효로 만듭니다(용량이 모자랄 때만 늘린다 — 세대가 한 바퀴 돌면 그때 한 번 지운다).
     *          큐는 머리 위치가 앞으로만 가는 배열이라 꺼낸 칸도 남습니다(`getVisitOrder` — 방문 순서 그대로).
     * @code
     *     _search.begin( topology.getCellCount() );
     *     _search.visit( topology.toIndex( start ), -1 );
     *     while ( _search.hasNext() )
     *     {
     *         const int32 index = _search.popNext();
     *         for ( int32 direction = 0; direction < GridTopology::kOrthogonalCount; ++direction )
     *             ... if ( 지나갈 수 있으면 ) _search.visit( topology.toIndex( next ), index );
     *     }
     * @endcode
     */
    class GridSearchScratch
    {
    public:
        /** @brief 새 탐색을 시작합니다 — 표시 · 큐를 비우고 @p cellCount 칸을 담을 수 있게 합니다. */
        void begin( int32 cellCount )
        {
            const size_t count = static_cast<size_t>( cellCount > 0 ? cellCount : 0 );
            if ( _listStamp.size() < count )
            {
                _listStamp.resize( count, 0u );
                _listParent.resize( count, -1 );
            }
            ++_generation;
            if ( _generation == 0u )
            {
                // 세대가 한 바퀴 돌았다 — 옛 표시가 새 세대와 겹치지 않게 지운다.
                for ( uint32& stamp : _listStamp )
                    stamp = 0u;
                _generation = 1u;
            }
            _listQueue.clear();
            _head = 0;
        }

        /** @brief 처음 보는 칸이면 표시 · 부모를 적고 큐에 넣은 뒤 true 입니다. 이미 봤으면 false 입니다. */
        bool visit( int32 index, int32 parentIndex )
        {
            uint32& stamp = _listStamp[static_cast<size_t>( index )];
            if ( stamp == _generation )
                return false;
            stamp                                     = _generation;
            _listParent[static_cast<size_t>( index )] = parentIndex;
            _listQueue.push_back( index );
            return true;
        }

        bool isVisited( int32 index ) const { return _listStamp[static_cast<size_t>( index )] == _generation; }
        /** @brief 이번 탐색에서 @p index 를 연 칸입니다(시작 칸은 −1). 보지 않은 칸이면 안 됩니다. */
        int32 getParent( int32 index ) const { return _listParent[static_cast<size_t>( index )]; }
        bool  hasNext() const { return _head < _listQueue.size(); }
        int32 popNext() { return _listQueue[_head++]; }
        /** @brief 이번 탐색에서 본 칸을 본 순서대로 돌려줍니다. */
        const vector<int32>& getVisitOrder() const { return _listQueue; }

    private:
        vector<uint32> _listStamp;  ///< 칸마다 마지막으로 본 세대
        vector<int32>  _listParent; ///< 칸마다 연 칸(이번 세대일 때만 뜻이 있다)
        vector<int32>  _listQueue;  ///< 본 순서 — `_head` 앞은 꺼냈다
        size_t         _head{ 0 };
        uint32         _generation{ 0u };
    };
} // namespace sw
