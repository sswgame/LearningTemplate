/**
 * @file EventBuffer.h
 * @brief 시뮬레이션이 한 걸음 동안 쌓고 게임이 프레임마다 가져가는 알림 버퍼입니다(`drainEvents` 의 몸통).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @class EventBuffer
     * @brief 알림을 쌓고(`push`) 받는 쪽 목록 끝에 넘긴 뒤 비웁니다(`drainTo`). 프레임마다 할당하지 않습니다.
     * @details 받는 목록이 비어 있으면 두 저장소를 맞바꿔 복사 없이 넘깁니다 — 받는 쪽이 같은 목록을 비워 다시 쓰면 두 버퍼가 번갈아 돌고,
     *          쌓이는 쪽은 받는 쪽이 쓰던 용량을 물려받습니다. 받는 목록에 이미 있으면 그 뒤에 붙입니다(여러 곳의 알림을 한 목록에 모으는 쪽).
     *          언리얼 `TQueue` 를 프레임마다 비우는 자리 · 유니티 이벤트 큐와 같은 몫이지만 스레드 안전하지 않습니다 — 한 스레드(게임 스레드)용입니다.
     * @code
     *     void RTSWorld::pushEvent( ... ) { _eventBuffer.push( event ); }
     *     void RTSWorld::drainEvents( vector<RTSEvent>& outListEvent ) { _eventBuffer.drainTo( outListEvent ); }
     * @endcode
     */
    template <typename T>
    class EventBuffer
    {
    public:
        void push( const T& event ) { _listPending.push_back( event ); }
        void push( T&& event ) { _listPending.push_back( std::move( event ) ); }

        /** @brief 쌓인 알림을 @p outList 끝에 넘기고 비웁니다. @p outList 가 비어 있으면 저장소를 맞바꿉니다(복사 없음). */
        void drainTo( vector<T>& outList )
        {
            if ( outList.empty() )
            {
                outList.swap( _listPending ); // 비어 있던 저장소(용량 그대로)가 이쪽으로 온다
                return;
            }
            outList.insert( outList.end(), _listPending.begin(), _listPending.end() );
            _listPending.clear();
        }

        /** @brief 쌓인 알림을 버립니다(용량은 둔다). */
        void clear() { _listPending.clear(); }

        /** @brief 마지막에 쌓은 알림입니다 — 쌓은 뒤 칸을 덧붙일 때 씁니다. 비어 있으면 안 됩니다. */
        T&               getLast() { return _listPending.back(); }
        bool             isEmpty() const { return _listPending.empty(); }
        size_t           getCount() const { return _listPending.size(); }
        const vector<T>& getPending() const { return _listPending; }

    private:
        vector<T> _listPending;
    };
} // namespace sw
