/**
 * @file TickRingBuffer.h
 * @brief 틱 번호로 찾는 링 버퍼를 정의합니다.
 * @details 최근 N틱의 기록(예측, 스냅샷, 랙 보정, 롤백 상태, 락스텝 입력, 체크섬)을 틱으로 넣고 찾는다.
 *          슬롯은 틱 % 크기로 정한다. 슬롯마다 들어 있는 틱도 함께 적어 두므로, 한 바퀴 뒤의 틱이 덮어쓴 슬롯을 찾으면 없다고 답한다.
 *          - 키는 32비트 단조 번호(틱, 프레임, 사건 수)다. 키 전체를 적으므로 감김 비교가 없고, 건너뛴 틱의 슬롯을 비울 필요도 없다.
 *            16비트로 감기는 패킷과 메시지 시퀀스에는 `SequenceBuffer`(낡은 것 거절, 건너뛴 슬롯 비우기)를 쓴다.
 *          - 어디부터 너무 낡았다고 볼지는 쓰는 쪽마다 다르다(가장 새 틱 기준, 현재 프레임 ± 윈도우, 확인한 틱의 나이).
 *            이 클래스는 "그 틱이 들어 있는가"만 답하고, 윈도우는 쓰는 쪽이 넣기 전에 확인한다. `computeOldestTick` 은 가장 새 틱 기준 윈도우의 아래 끝이다.
 *          - `acquire` 는 슬롯의 옛 값을 비우지 않는다. 값 안의 버퍼(스냅샷 엔티티, 상태 바이트) 용량을 다시 쓰기 위해서이며, 부르는 쪽이 값을 모두 덮어써야 한다.
 *          - `kEmpty` 틱은 빈 슬롯 표시이므로 넣어도 찾을 수 없다.
 *          - 언리얼 `FRepChangelistState` 의 변경 기록 링, GGPO 의 입력 큐와 저장 상태 링, 유니티 Netcode `CommandDataUtility`(틱으로 찾는 명령 링)에 해당한다.
 *          - 스레드 안전하지 않다(쓰는 쪽 하나). 읽기만 하는 `find` 와 `get*` 는 쓰기와 겹치지 않으면 여러 스레드가 함께 불러도 된다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class TickRingBuffer
     * @brief 슬롯은 틱 % 크기로 정합니다. `initialize` 전에는 슬롯이 없어 무엇도 찾을 수 없습니다.
     */
    template <typename T>
    class TickRingBuffer
    {
    public:
        static constexpr uint32 kEmpty = invalid_index::kUint32;

        TickRingBuffer()
            : _listEntry{}
            , _listTick{}
            , _count{ 0 }
            , _newestTick{ 0 }
            , _bHasNewest{ false }
        {
        }

        /** @brief @p capacity 개(1보다 작으면 1)의 슬롯을 새 값으로 만들고 비웁니다. */
        void initialize( int32 capacity )
        {
            const size_t size = static_cast<size_t>( capacity > 0 ? capacity : 1 );
            _listEntry.assign( size, T{} );
            _listTick.assign( size, kEmpty );
            _count      = 0;
            _newestTick = 0;
            _bHasNewest = false;
        }

        /** @brief 넣은 틱을 모두 잊습니다. 값(버퍼 용량)은 슬롯에 남아 다음 `acquire` 가 다시 씁니다. */
        void reset()
        {
            _listTick.assign( _listTick.size(), kEmpty );
            _count      = 0;
            _newestTick = 0;
            _bHasNewest = false;
        }

        /**
         * @brief @p tick 의 슬롯을 잡아 반환합니다. 그 슬롯의 다른 틱(한 바퀴 앞)은 잊습니다. 옛 값은 비우지 않으므로 부르는 쪽이 모두 덮어써야 합니다.
         * @warning `initialize` 전에는 부르지 않는다(슬롯이 없다).
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

        /** @brief 그 틱을 잊습니다. 없거나 덮였으면 그대로 둡니다. 가장 새 틱은 줄지 않습니다. */
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

        /** @brief 그 틱의 값을 반환합니다. 넣은 적이 없거나 한 바퀴 뒤의 틱이 덮어썼으면 nullptr 입니다. */
        T* find( uint32 tick )
        {
            if ( tick == kEmpty || _listTick.empty() )
                return nullptr;
            const size_t index = computeIndex( tick );
            return _listTick[index] == tick ? &_listEntry[index] : nullptr;
        }
        const T* find( uint32 tick ) const { return const_cast<TickRingBuffer*>( this )->find( tick ); }
        bool     exists( uint32 tick ) const { return find( tick ) != nullptr; }

        /** @brief 가장 새 틱 기준으로 보관할 수 있는 가장 오래된 틱을 반환합니다(가장 새 틱 - (크기 - 1), 0 아래로 내려가지 않습니다). 그 사이에도 빈 틱은 있을 수 있습니다. */
        uint32 computeOldestTick() const
        {
            const uint32 span = _listTick.empty() ? 0u : static_cast<uint32>( _listTick.size() - 1 );
            return _newestTick >= span ? _newestTick - span : 0u;
        }
        /** @brief 넣은 틱 중 가장 큰 것을 반환합니다. 지운 뒤에도 줄지 않으며 `reset` 만 0으로 되돌립니다. */
        uint32 getNewestTick() const { return _newestTick; }
        bool   hasNewest() const { return _bHasNewest; }
        int32  getCapacity() const { return static_cast<int32>( _listTick.size() ); }
        /** @brief 지금 들어 있는(덮이지도 지워지지도 않은) 틱 수를 반환합니다. */
        int32 getCount() const { return _count; }

    private:
        size_t computeIndex( uint32 tick ) const { return static_cast<size_t>( tick % static_cast<uint32>( _listTick.size() ) ); }

        vector<T>      _listEntry;
        vector<uint32> _listTick; ///< 슬롯마다 들어 있는 틱(`kEmpty` 는 빈 슬롯).
        int32          _count;
        uint32         _newestTick;
        bool           _bHasNewest;
    };
} // namespace sw
