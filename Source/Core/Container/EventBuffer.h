/**
 * @file EventBuffer.h
 * @brief 시뮬레이션이 한 걸음 동안 쌓고 게임이 프레임마다 가져가는 알림 버퍼입니다(`drainEvents` 의 몸통).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/DrainGate.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @class EventBuffer
     * @brief 알림을 쌓고(`push`) 받는 쪽 목록 끝에 넘긴 뒤 비웁니다(`drainTo`). 프레임마다 할당하지 않습니다.
     * @details 받는 목록이 비어 있으면 두 저장소를 맞바꿔 복사 없이 넘깁니다 — 받는 쪽이 같은 목록을 비워 다시 쓰면 두 버퍼가 번갈아 돌고,
     *          쌓이는 쪽은 받는 쪽이 쓰던 용량을 물려받습니다. 받는 목록에 이미 있으면 그 뒤에 붙입니다(여러 곳의 알림을 한 목록에 모으는 쪽).
     *          언리얼 `TQueue` 를 프레임마다 비우는 용도 · 유니티 이벤트 큐와 같은 역할이지만 스레드 안전하지 않습니다 — 한 스레드(게임 스레드)용입니다.
     * @code
     *     void RTSWorld::pushEvent( ... ) { _eventBuffer.push( event ); }
     *     void RTSWorld::drainEvents( vector<RTSEvent>& outListEvent ) { _eventBuffer.drainTo( outListEvent ); }
     * @endcode
     */
    template <typename T>
    class EventBuffer
    {
    public:
        EventBuffer() = default;
        // 복사 · 이동은 쌓인 알림만 옮긴다 — 꺼내기 권한 표(`_gate`)는 복사본에 따라가지 않는다(복사본은 아무도 안 쥔 새 큐).
        EventBuffer( const EventBuffer& other )
            : _listPending{ other._listPending } {}
        EventBuffer( EventBuffer&& other ) noexcept
            : _listPending{ std::move( other._listPending ) } {}
        EventBuffer& operator=( const EventBuffer& other )
        {
            _listPending = other._listPending;
            return *this;
        }
        EventBuffer& operator=( EventBuffer&& other ) noexcept
        {
            _listPending = std::move( other._listPending );
            return *this;
        }

        void push( const T& event ) { _listPending.push_back( event ); }
        void push( T&& event ) { _listPending.push_back( std::move( event ) ); }

        /**
         * @brief 꺼내는 쪽을 @p outLease 하나로 정합니다. 이미 다른 표가 쥐고 있으면 false 입니다(어서션도 남깁니다). 표를 쥔 큐는 표 없는 `drainTo` 가 어서션에 걸립니다.
         * @details 여러 화면 · 시스템이 같은 규칙 모델을 붙일 수 있는 큐는 이 표로 막는다. 소유 컴포넌트 하나가 비우는 큐는 표 없는 `drainTo` 를 그대로 쓴다.
         */
        [[nodiscard]] bool claim( DrainLease& outLease ) { return _gate.claim( outLease ); }

        /** @brief 표를 쥔 쪽이 꺼냅니다. @p lease 가 이 큐의 표가 아니면 어서션입니다. 나머지는 표 없는 `drainTo` 와 같습니다. */
        void drainTo( [[maybe_unused]] const DrainLease& lease, vector<T>& outList )
        {
            SW_ASSERT( _gate.isHeldBy( lease ) && "Claim the drain queue before draining it" );
            moveTo( outList );
        }

        /** @brief 쌓인 알림을 @p outList 끝에 넘기고 비웁니다. @p outList 가 비어 있으면 저장소를 맞바꿉니다(복사 없음). 표를 쥔 큐에는 쓸 수 없습니다. */
        void drainTo( vector<T>& outList )
        {
            SW_ASSERT( _gate.hasHolder() == false && "This drain queue is gated; drain it with the lease" );
            moveTo( outList );
        }

        /** @brief 쌓인 알림을 버립니다(용량은 둔다). */
        void clear() { _listPending.clear(); }

        /** @brief 마지막에 쌓은 알림입니다 — 쌓은 뒤 항목을 덧붙일 때 씁니다. 비어 있으면 안 됩니다. */
        T&               getLast() { return _listPending.back(); }
        bool             isEmpty() const { return _listPending.empty(); }
        size_t           getCount() const { return _listPending.size(); }
        const vector<T>& getPending() const { return _listPending; }

    private:
        void moveTo( vector<T>& outList )
        {
            if ( outList.empty() )
            {
                outList.swap( _listPending ); // 비어 있던 저장소(용량 그대로)가 이쪽으로 온다
                return;
            }
            outList.insert( outList.end(), _listPending.begin(), _listPending.end() );
            _listPending.clear();
        }

        vector<T> _listPending;
        DrainGate _gate;
    };
} // namespace sw
