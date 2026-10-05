/**
 * @file TickRingBuffer.h
 * @brief 틱 번호로 찾는 고리 버퍼입니다 — 최근 N 틱의 기록(예측 · 스냅숏 · 랙 보정 · 롤백 상태 · 락스텝 입력 · 체크섬)을 틱으로 넣고 찾습니다.
 * @details 자리 = 틱 % 크기 입니다. 자리마다 들어 있는 틱을 같이 적어 두어, 한 바퀴 뒤의 틱이 덮은 자리를 찾으면 없다고 합니다.
 *          - 키는 32 비트 단조 번호(틱 · 프레임 · 사건 수)입니다. 키 전체를 적으므로 감김 비교가 없고 건너뛴 틱의 자리를 비울 필요도 없다.
 *            16 비트로 감기는 패킷 · 메시지 시퀀스는 `SequenceBuffer`(낡음 거절 · 건너뛴 칸 비우기)를 쓴다.
 *          - 무엇이 너무 낡았나는 쓰는 쪽마다 다르다(가장 새 틱 기준 · 지금 프레임 ± 창 · 확인한 틱의 나이) — 이 부품은 "그 틱이 들어 있나" 만 답하고,
 *            창은 쓰는 쪽이 넣기 전에 본다. `computeOldestTick` 은 가장 새 틱 기준 창의 아래 끝입니다.
 *          - `acquire` 는 자리의 옛 값을 비우지 않습니다 — 값 안의 버퍼(스냅숏 엔티티 · 상태 바이트) 용량을 다시 쓴다. 부르는 쪽이 값을 모두 덮어쓴다.
 *          - `kEmpty` 틱은 빈 자리 표시라 넣어도 찾을 수 없다.
 *          언리얼 `FRepChangelistState` 의 변경 기록 고리 · GGPO 의 입력 큐 · 저장 상태 고리 · 유니티 Netcode `CommandDataUtility`(틱으로 찾는 명령 고리)의
 *          자리입니다. 스레드 안전하지 않다(쓰는 쪽 하나 — 읽기만 하는 `find` · `get*` 는 쓰기와 겹치지 않으면 여러 스레드가 같이 불러도 된다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @class TickRingBuffer
     * @brief 자리 = 틱 % 크기 입니다. `initialize` 전에는 자리가 없어 무엇도 찾지 못합니다.
     */
    template <typename T>
    class TickRingBuffer
    {
    public:
        static constexpr uint32 kEmpty = 0xFFFFFFFFu;

        TickRingBuffer()
            : _listEntry{}
            , _listTick{}
            , _count{ 0 }
            , _newestTick{ 0 }
            , _bHasNewest{ false }
        {
        }

        /** @brief @p capacity 칸(1 보다 작으면 1)을 새 값으로 잡고 비웁니다. */
        void initialize( int32 capacity )
        {
            const size_t size = static_cast<size_t>( capacity > 0 ? capacity : 1 );
            _listEntry.assign( size, T{} );
            _listTick.assign( size, kEmpty );
            _count      = 0;
            _newestTick = 0;
            _bHasNewest = false;
        }

        /** @brief 넣은 틱을 모두 잊습니다. 값(버퍼 용량)은 자리에 남아 다음 `acquire` 가 다시 쓴다. */
        void reset()
        {
            _listTick.assign( _listTick.size(), kEmpty );
            _count      = 0;
            _newestTick = 0;
            _bHasNewest = false;
        }

        /**
         * @brief @p tick 의 자리를 잡아 돌려줍니다. 그 자리의 다른 틱(한 바퀴 앞)은 잊는다. 옛 값은 비우지 않는다 — 부르는 쪽이 모두 덮어쓴다.
         * @warning `initialize` 전에는 부르지 않는다(자리가 없다).
         */
        T& acquire( uint32 tick )
        {
            SW_ASSERT( _listTick.empty() == false );
            const size_t index    = computeIndex( tick );
            const bool   bWasLive = _listTick[index] != kEmpty;
            const bool   bIsLive  = tick != kEmpty;
            _listTick[index]      = tick;
            _count += ( bIsLive ? 1 : 0 ) - ( bWasLive ? 1 : 0 );
            if ( _bHasNewest == false || tick > _newestTick )
            {
                _newestTick = tick;
                _bHasNewest = true;
            }
            return _listEntry[index];
        }

        /** @brief 그 틱을 잊습니다. 없거나 덮였으면 그대로입니다. 가장 새 틱은 줄지 않는다. */
        void remove( uint32 tick )
        {
            if ( tick == kEmpty || _listTick.empty() )
                return;
            const size_t index = computeIndex( tick );
            if ( _listTick[index] != tick )
                return;
            _listTick[index] = kEmpty;
            --_count;
        }

        /** @brief 그 틱의 값입니다. 넣은 적 없거나 한 바퀴 뒤의 틱이 덮었으면 nullptr 입니다. */
        T* find( uint32 tick )
        {
            if ( tick == kEmpty || _listTick.empty() )
                return nullptr;
            const size_t index = computeIndex( tick );
            return _listTick[index] == tick ? &_listEntry[index] : nullptr;
        }
        const T* find( uint32 tick ) const { return const_cast<TickRingBuffer*>( this )->find( tick ); }
        bool     exists( uint32 tick ) const { return find( tick ) != nullptr; }

        /** @brief 가장 새 틱 기준으로 들 수 있는 가장 오래된 틱(가장 새 틱 − (크기 − 1), 0 아래로 내려가지 않는다)입니다. 그 사이에도 빈 틱은 있을 수 있다. */
        uint32 computeOldestTick() const
        {
            const uint32 span = _listTick.empty() ? 0u : static_cast<uint32>( _listTick.size() - 1 );
            return _newestTick >= span ? _newestTick - span : 0u;
        }
        /** @brief 넣은 틱 중 가장 큰 것입니다(지운 뒤에도 줄지 않는다 — `reset` 만 0 으로). */
        uint32 getNewestTick() const { return _newestTick; }
        bool   hasNewest() const { return _bHasNewest; }
        int32  getCapacity() const { return static_cast<int32>( _listTick.size() ); }
        /** @brief 지금 들어 있는(덮이지도 지워지지도 않은) 틱 수입니다. */
        int32 getCount() const { return _count; }

    private:
        size_t computeIndex( uint32 tick ) const { return static_cast<size_t>( tick % static_cast<uint32>( _listTick.size() ) ); }

        vector<T>      _listEntry;
        vector<uint32> _listTick; ///< 자리마다 들어 있는 틱(`kEmpty` = 비었다)
        int32          _count;
        uint32         _newestTick;
        bool           _bHasNewest;
    };
} // namespace sw
