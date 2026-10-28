/**
 * @file ClientPrediction.h
 * @brief 클라이언트 예측과 재조정을 합니다. 내 입력을 바로 적용해 그리고, 서버가 "그 입력까지 처리한 상태" 를 보냈을 때 예측과 다르면 거기서부터 남은 입력으로 다시 시뮬레이션합니다.
 * @details 상태와 입력 타입은 게임이 정하는 템플릿 인자이다.
 *          시뮬레이션 함수는 서버와 같은 코드여야 한다. 다르면 매번 재조정하게 된다.
 *          서버 상태와 그 입력 틱은 스냅샷의 소유자 상태 칸(`NetOwnerState`, `ReplicationClient::findOwnerState`)에서 한 쌍으로 받는다.
 *          - 기록은 틱을 하나씩 이어서 넣는다(`record`). 빈 틱이 있으면 다시 시뮬레이션이 입력을 건너뛰므로 이어지지 않는 틱은 거절한다.
 *          - 재조정 결과는 `PredictionReconcileResult` 넷 중 하나이다. "맞았다" 와 "맞출 기록이 없다" 를 같은 false 로 돌려주면
 *            기록이 밀려나 예측이 영영 맞춰지지 않아도 부르는 쪽이 알 수 없었다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Replication/TickRingBuffer.h"

namespace sw
{
    /** @brief `ClientPrediction::reconcile` 의 결과입니다. */
    enum class PredictionReconcileResult : uint8
    {
        Matched,        ///< 예측이 서버 상태와 맞았다. 아무것도 바꾸지 않았다
        Corrected,      ///< 서버 상태에서 남은 입력을 다시 시뮬레이션해 지금 상태와 기록을 고쳤다
        TickAhead,      ///< 서버 틱이 마지막으로 기록한 틱보다 앞이거나 아직 기록이 없다. 틱 번호를 서버와 다르게 세고 있다는 뜻이다. 아무것도 바꾸지 않았다
        TickOverwritten ///< 그 틱의 기록이 고리에서 밀려났다. 예측이 기록 크기보다 앞서 있다. 아무것도 바꾸지 않았으며 게임은 서버 상태로 바로 맞추거나 기록 크기를 키운다
    };
} // namespace sw

namespace sw
{
    template <typename TState, typename TInput>
    class ClientPrediction
    {
    public:
        /** @brief @p capacity 틱의 기록을 둡니다. 2보다 작으면 다시 시뮬레이션할 틱이 남지 않으므로 받지 않습니다. */
        explicit ClientPrediction( int32 capacity = 128 )
        {
            SW_ASSERT( capacity >= 2 && "ClientPrediction needs room for the server tick and at least one predicted tick" );
            _listEntry.initialize( capacity );
        }

        /**
         * @brief 이 틱에 이 입력을 적용해 나온 상태를 기억합니다.
         * @return 첫 기록이 아닌데 마지막으로 기록한 틱의 바로 다음 틱이 아니면 기록하지 않고 false 를 반환합니다. 빈 틱이 생기면 다시 시뮬레이션이 그 입력을 건너뛰기 때문입니다.
         */
        [[nodiscard]] bool record( uint32 tick, const TInput& input, const TState& stateAfter )
        {
            const bool bContiguous = _bHasRecord == false || tick == _latestTick + 1u;
            if ( bContiguous == false )
                return false;
            Entry& entry      = _listEntry.acquire( tick );
            entry._input      = input;
            entry._stateAfter = stateAfter;
            _latestTick       = tick;
            _bHasRecord       = true;
            return true;
        }

        /**
         * @brief 서버의 권위 상태(입력 @p serverTick 까지 처리한 것)를 받아 예측을 맞춥니다. 예측이 맞으면 그대로 두고, 틀리면 다시 시뮬레이션한 최신 상태를 @p inoutCurrent 에 둡니다.
         * @param simulate `TState( const TState&, const TInput& )` 형태이며 한 틱을 진행합니다.
         * @param isClose  `bool( const TState&, const TState& )` 형태이며 두 상태가 같다고 볼 만큼 가까운지 판정합니다.
         */
        template <typename TSimulate, typename TIsClose>
        [[nodiscard]] PredictionReconcileResult reconcile( uint32 serverTick, const TState& serverState, TState& inoutCurrent, TSimulate&& simulate, TIsClose&& isClose )
        {
            if ( _bHasRecord == false || serverTick > _latestTick )
                return PredictionReconcileResult::TickAhead;
            Entry* pEntry = _listEntry.find( serverTick );
            if ( pEntry == nullptr )
                return PredictionReconcileResult::TickOverwritten;
            if ( isClose( pEntry->_stateAfter, serverState ) )
                return PredictionReconcileResult::Matched;
            // 서버 상태에서 다시 시작하고 기록도 고친다. 기록은 이어져 있고 서버 틱이 남아 있으므로 그 뒤 틱도 모두 남아 있다.
            TState state        = serverState;
            pEntry->_stateAfter = serverState;
            for ( uint32 tick = serverTick + 1u; tick <= _latestTick; ++tick )
            {
                Entry* pNext = _listEntry.find( tick );
                if ( pNext == nullptr )
                    ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
                state              = simulate( state, pNext->_input );
                pNext->_stateAfter = state;
            }
            inoutCurrent = state;
            ++_correctionCount;
            return PredictionReconcileResult::Corrected;
        }

        uint32 getCorrectionCount() const { return _correctionCount; }
        uint32 getLatestTick() const { return _latestTick; }
        bool   hasRecord() const { return _bHasRecord; }

    private:
        struct Entry
        {
            TState _stateAfter{};
            TInput _input{};
        };

        TickRingBuffer<Entry> _listEntry{};
        uint32                _latestTick{ 0 }; ///< 마지막으로 기록한 틱. 다시 시뮬레이션하는 범위가 여기까지이다
        uint32                _correctionCount{ 0 };
        bool                  _bHasRecord{ false };
    };
} // namespace sw
