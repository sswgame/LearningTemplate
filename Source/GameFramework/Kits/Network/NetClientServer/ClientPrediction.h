/**
 * @file ClientPrediction.h
 * @brief 클라이언트 예측 · 되맞추기 — 내 입력을 바로 적용해 그리고, 서버가 "그 입력까지 처리한 상태" 를 보내면 다르면 거기서부터 남은 입력으로 다시 흘립니다.
 * @details 상태 · 입력 타입은 게임의 것입니다(템플릿). 시뮬레이션 함수는 서버와 같은 코드여야 합니다 — 다르면 매번 되맞추게 된다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Network/Replication/TickRingBuffer.h"

namespace sw
{
    template <typename TState, typename TInput>
    class ClientPrediction
    {
    public:
        explicit ClientPrediction( int32 capacity = 128 )
        {
            _listEntry.initialize( capacity > 1 ? capacity : 2 );
        }

        /** @brief 이 틱에 이 입력을 적용해 나온 상태를 기억합니다. */
        void record( uint32 tick, const TInput& input, const TState& stateAfter )
        {
            Entry& entry      = _listEntry.acquire( tick );
            entry._input      = input;
            entry._stateAfter = stateAfter;
            _latestTick       = tick; // 마지막으로 기록한 틱 — 다시 흘리기는 여기까지(가장 큰 틱이 아니다)
        }

        /**
         * @brief 서버의 권위 상태(입력 @p serverTick 까지 처리)를 받아 맞춥니다. 예측이 맞으면 그대로, 틀리면 다시 흘린 최신 상태를 @p inoutCurrent 에 둡니다.
         * @param simulate `TState( const TState&, const TInput& )` — 한 틱.
         * @param isClose  `bool( const TState&, const TState& )` — 같다고 볼 만큼 가까운가.
         * @return 되맞췄으면 true.
         */
        template <typename TSimulate, typename TIsClose>
        bool reconcile( uint32 serverTick, const TState& serverState, TState& inoutCurrent, TSimulate&& simulate, TIsClose&& isClose )
        {
            Entry* pEntry = _listEntry.find( serverTick );
            if ( pEntry == nullptr || serverTick > _latestTick )
                return false;
            if ( isClose( pEntry->_stateAfter, serverState ) )
                return false;
            // 서버 상태에서 다시 — 기록도 고친다.
            TState state        = serverState;
            pEntry->_stateAfter = serverState;
            for ( uint32 tick = serverTick + 1u; tick <= _latestTick; ++tick )
            {
                Entry* pNext = _listEntry.find( tick );
                if ( pNext == nullptr )
                    break;
                state              = simulate( state, pNext->_input );
                pNext->_stateAfter = state;
            }
            inoutCurrent = state;
            ++_correctionCount;
            return true;
        }

        uint32 getCorrectionCount() const { return _correctionCount; }
        uint32 getLatestTick() const { return _latestTick; }

    private:
        struct Entry
        {
            TState _stateAfter{};
            TInput _input{};
        };

        TickRingBuffer<Entry> _listEntry{};
        uint32                _latestTick{ 0 };
        uint32                _correctionCount{ 0 };
    };
} // namespace sw
