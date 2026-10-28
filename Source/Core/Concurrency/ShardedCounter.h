/**
 * @file ShardedCounter.h
 * @brief 쓰는 스레드는 자기 칸(캐시 라인)에만 더하고, 읽는 쪽이 칸을 모두 합치는 카운터와 그 칸 배열입니다.
 * @details 여러 스레드가 원자 변수 하나에 `fetch_add` 하면 그 캐시 라인이 코어 사이를 오가서 연산마다 수십~수백 사이클이 듭니다.
 *          칸을 스레드마다 나누면 쓰기는 경합 없는 원자 연산 하나가 되고, 값이 필요한 드문 읽기가 칸 수만큼 읽습니다.
 *          언리얼 LLM 이 스레드별 추적 상태를 두고 프레임마다 합치는 방식, 리눅스 커널 `percpu_counter` 와 같은 구조입니다.
 *
 *          계약:
 *          - 합은 한 순간의 값이 아닙니다. 칸을 차례로 읽는 동안 다른 스레드가 더하므로, 읽기와 겹친 변화는 들어갈 수도 빠질 수도 있습니다.
 *          - **"0 이 됐나" 를 보는 카운터는 올리고 내리는 짝을 같은 칸에서 합니다.** 올린 칸과 내린 칸이 다르면, 합을 읽는 도중에 생겨서
 *            끝난 항목이 내린 몫만 보여 합이 실제보다 작아지고, 살아 있는 항목이 있는데도 0 을 볼 수 있습니다. 짝을 같은 칸에 두면 칸마다
 *            값이 "그 칸에서 생겨 아직 살아 있는 수" 라 0 아래로 내려가지 않고, 읽기 내내 살아 있던 항목은 반드시 셉니다.
 *            올릴 때 `ShardedCounterUtil::getCurrentThreadShard()` 를 받아 항목에 적어 두고, 내릴 때 그 번호로 `addToShard` 를 부릅니다.
 *          - 칸 수보다 스레드가 많으면 칸을 나눠 씁니다. 칸은 원자 값이라 값은 맞고, 그 칸에서만 경합이 생깁니다.
 *          - 누계처럼 내려가지 않는 값은 아무 칸에나 더해도 됩니다(`add`).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct ShardedCounterUtil
     * @brief 칸 수와 지금 스레드의 칸 번호입니다. 칸 배열(`ShardArray`) · 카운터(`ShardedCounter`)가 함께 씁니다.
     */
    struct SW_API ShardedCounterUtil
    {
        /**
         * @brief 칸 수입니다. 2 의 거듭제곱이어야 합니다.
         * @details 16 하드웨어 스레드 PC 에서 워커 14 + 게임 · 렌더 스레드가 대부분 제 칸을 갖는 수입니다. 늘리면 합을 읽는 비용(칸마다 캐시
         *          라인 하나)과 카운터마다의 메모리(칸 × 64 B)가 같이 늡니다.
         */
        static constexpr uint32 kShardCount = 16;
        /** @brief 칸 번호를 칸 수 안으로 접는 마스크입니다. */
        static constexpr uint32 kShardMask = kShardCount - 1;
        static_assert( ( kShardCount & kShardMask ) == 0, "kShardCount must be a power of two" );

        /**
         * @brief 지금 스레드의 칸 번호(0 ~ `kShardCount` - 1)입니다.
         * @details 스레드가 처음 물을 때 프로세스 전체 차례로 하나 받고, 그 스레드가 사는 동안 바꾸지 않습니다. 먼저 물은 스레드부터 칸을 따로
         *          가지므로 워커 풀처럼 한꺼번에 만든 스레드는 서로 겹치지 않습니다.
         */
        static uint32 getCurrentThreadShard();
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShardArray
     * @brief 칸마다 @p TBlock 하나를 캐시 라인 경계에 둔 배열입니다. 칸 하나에 값 여럿(태그별 통계처럼)을 모을 때 씁니다.
     * @details @p TBlock 의 필드는 원자 값이어야 합니다. 칸 수보다 스레드가 많으면 두 스레드가 한 칸을 함께 씁니다.
     */
    template <typename TBlock>
    class ShardArray
    {
    public:
        /** @brief 칸 @p shardIndex 의 블록입니다. 번호는 칸 수로 접습니다. */
        TBlock& getShard( uint32 shardIndex ) { return _arrSlot[shardIndex & ShardedCounterUtil::kShardMask]._block; }
        /** @brief 칸 @p shardIndex 의 블록입니다(읽기). */
        const TBlock& getShard( uint32 shardIndex ) const { return _arrSlot[shardIndex & ShardedCounterUtil::kShardMask]._block; }
        /** @brief 지금 스레드의 칸입니다. */
        TBlock& getCurrentThreadShard() { return getShard( ShardedCounterUtil::getCurrentThreadShard() ); }

    private:
        /** @brief 칸 하나입니다. 이웃 칸과 캐시 라인을 나눠 쓰지 않게(false sharing) 64 바이트 경계에서 시작합니다. */
        struct alignas( 64 ) Slot
        {
            TBlock _block{};
        };

        Slot _arrSlot[ShardedCounterUtil::kShardCount]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShardedCounter
     * @brief 칸마다 원자 값 하나를 둔 카운터입니다. 쓰기는 자기 칸에, 읽기(`sum`)는 칸 전체를 합칩니다.
     * @details @p T 는 정수입니다. 칸 하나는 다른 칸에서 올린 것을 내려 음수(부호 없는 형식이면 감긴 값)가 될 수 있지만, 합은 정수 덧셈이라
     *          맞습니다. "0 이 됐나" 를 볼 때의 짝 규칙은 파일 머리 설명에 있습니다.
     */
    template <typename T>
    class ShardedCounter
    {
    public:
        /** @brief 지금 스레드의 칸에 @p delta 를 더합니다. */
        void add( T delta, std::memory_order order = std::memory_order_relaxed ) { addToShard( ShardedCounterUtil::getCurrentThreadShard(), delta, order ); }
        /** @brief 칸 @p shardIndex 에 @p delta 를 더합니다. 올린 칸에서 내려야 할 때 씁니다. */
        void addToShard( uint32 shardIndex, T delta, std::memory_order order = std::memory_order_relaxed ) { _shard.getShard( shardIndex ).fetch_add( delta, order ); }

        /**
         * @brief 모든 칸의 합입니다. 칸 수만큼 원자 읽기를 합니다.
         * @param order 칸마다의 읽기 순서입니다. 다른 원자 값과 Dekker 식 짝을 이루는 확인(잠들기 전 조건 확인)이면 `seq_cst` 를 줍니다.
         */
        T sum( std::memory_order order = std::memory_order_relaxed ) const
        {
            T total{ 0 };
            for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
            {
                total += _shard.getShard( shardIndex ).load( order );
            }
            return total;
        }

        /** @brief 모든 칸을 0 으로 놓습니다. 쓰는 스레드가 없을 때만 부릅니다(그 사이의 더하기는 사라질 수 있다). */
        void reset()
        {
            for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
            {
                _shard.getShard( shardIndex ).store( 0, std::memory_order_relaxed );
            }
        }

    private:
        ShardArray<atomic<T>> _shard;
    };
} // namespace sw
