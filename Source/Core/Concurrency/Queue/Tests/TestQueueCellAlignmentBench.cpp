/**
 * @file TestQueueCellAlignmentBench.cpp
 * @brief 큐 칸 캐시 줄 정렬 판정용 벤치입니다. `ConcurrentQueue` 의 칸은 `alignas( 64 )` 라 8 바이트 항목 하나가 64 바이트를 쓴다 — 칸 정렬을 뺀 같은 링과
 *        처리량 · 칸 크기를 나란히 잰다. 시간 문턱은 두지 않고 값을 로그(`QueueCellBenchTest.*`)로 남긴다.
 * @details 판정(DesignBacklog "구조 · 성능 발전 방향" Core 의 측정 뒤 항목): 정렬을 뺀 판의 처리량이 3% 넘게 떨어지지 않으면 칸 정렬을 뺀다
 *          (Vyukov 원본 · folly `MPMCQueue` 는 칸마다 정렬하지 않고 머리 · 꼬리 위치만 떼어 놓는다 — 이 링도 그렇다).
 *          재는 법: Release(배포와 같은 자료 배치에 가깝게 — 검사 구성의 차이는 함께 적는다) `App --test --test-run-disabled --test-filter=QueueCellBenchTest.*` 5 번, 중앙값.
 *          제품 큐를 고치지 않으려고 같은 알고리즘(시퀀스 번호 링)을 칸 정렬만 바꿀 수 있게 여기 다시 적었다 — 판정 뒤 `ConcurrentQueueInternal::Cell` 한 줄을 바꾼다.
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include <array>
#include <thread>

namespace
{
    /** @brief 칸 정렬만 바꿀 수 있는 시퀀스 번호 링입니다(`ConcurrentQueue` 와 같은 알고리즘). */
    template <size_t CellAlignment, uint32 Capacity>
    class QueueCellBenchRingInternal
    {
    public:
        struct alignas( CellAlignment ) Cell
        {
            sw::atomic<uint32> _sequence{ 0 };
            uintptr_t          _data{ 0 };
        };

        QueueCellBenchRingInternal()
        {
            for ( uint32 index = 0; index < Capacity; ++index )
            {
                _arrCell[index]._sequence.store( index, std::memory_order_relaxed );
            }
        }

        bool enqueue( uintptr_t item )
        {
            uint32 pos = _enqueuePos.load( std::memory_order_relaxed );
            for ( ;; )
            {
                Cell&        cell = _arrCell[pos & ( Capacity - 1 )];
                const uint32 seq  = cell._sequence.load( std::memory_order_acquire );
                const int32  diff = static_cast<int32>( seq - pos );
                if ( diff == 0 )
                {
                    if ( _enqueuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
                    {
                        cell._data = item;
                        cell._sequence.store( pos + 1, std::memory_order_release );
                        return true;
                    }
                }
                else if ( diff < 0 )
                    return false;
                else
                    pos = _enqueuePos.load( std::memory_order_relaxed );
            }
        }

        bool dequeue( uintptr_t& outItem )
        {
            uint32 pos = _dequeuePos.load( std::memory_order_relaxed );
            for ( ;; )
            {
                Cell&        cell = _arrCell[pos & ( Capacity - 1 )];
                const uint32 seq  = cell._sequence.load( std::memory_order_acquire );
                const int32  diff = static_cast<int32>( seq - ( pos + 1 ) );
                if ( diff == 0 )
                {
                    if ( _dequeuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
                    {
                        outItem = cell._data;
                        cell._sequence.store( pos + Capacity, std::memory_order_release );
                        return true;
                    }
                }
                else if ( diff < 0 )
                    return false;
                else
                    pos = _dequeuePos.load( std::memory_order_relaxed );
            }
        }

    private:
        std::array<Cell, Capacity> _arrCell{};
        alignas( 64 ) sw::atomic<uint32> _enqueuePos{ 0 };
        alignas( 64 ) sw::atomic<uint32> _dequeuePos{ 0 };
    };

    struct QueueCellBenchInternal
    {
        static constexpr uint32 kCapacity      = 4096; ///< 태스크 Normal 전역 큐와 같은 용량
        static constexpr uint32 kProducerCount = 4;
        static constexpr uint32 kConsumerCount = 4;
        static constexpr uint32 kPerProducer   = 500000;

        /** @brief 생산자 · 소비자 넷씩이 링 하나로 항목을 모두 옮기는 시간(나노초)입니다. 옮긴 수가 맞지 않으면 -1 입니다. */
        template <typename TRing>
        static int64 measureTransferNanos()
        {
            sw::unique_ptr<TRing>   pRing = sw::make_unique<TRing>();
            sw::atomic<uint64>      takenCount{ 0 };
            sw::atomic<uint64>      takenSum{ 0 };
            constexpr uint64        kTotal = static_cast<uint64>( kProducerCount ) * kPerProducer;
            sw::vector<std::thread> listThread;
            const sw::Stopwatch     watch;
            for ( uint32 producer = 0; producer < kProducerCount; ++producer )
            {
                listThread.emplace_back( [&pRing]()
                {
                    for ( uint32 index = 1; index <= kPerProducer; ++index )
                    {
                        while ( pRing->enqueue( static_cast<uintptr_t>( index ) ) == false )
                        {
                            std::this_thread::yield();
                        }
                    }
                } );
            }
            for ( uint32 consumer = 0; consumer < kConsumerCount; ++consumer )
            {
                listThread.emplace_back( [&pRing, &takenCount, &takenSum]()
                {
                    uintptr_t item = 0;
                    while ( takenCount.load( std::memory_order_relaxed ) < kTotal )
                    {
                        if ( pRing->dequeue( item ) )
                        {
                            takenSum.fetch_add( item, std::memory_order_relaxed );
                            takenCount.fetch_add( 1, std::memory_order_relaxed );
                        }
                    }
                } );
            }
            for ( std::thread& thread : listThread )
            {
                thread.join();
            }
            const int64  nanos       = watch.getElapsedNanoseconds();
            const uint64 expectedSum = static_cast<uint64>( kProducerCount ) * ( static_cast<uint64>( kPerProducer ) * ( kPerProducer + 1 ) / 2 );
            return takenSum.load() == expectedSum ? nanos : -1;
        }
    };
} // namespace

/**
 * @brief [QueueCellBenchTest] 칸 64 바이트 정렬 링과 정렬 없는 링(칸 = 원자 + 항목)의 생산자 4 · 소비자 4 처리량 · 칸 크기입니다.
 */
SW_TEST_CASE( QueueCellBenchTest, CacheLineAlignedCellsVersusPacked, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 600 )
{
    using AlignedRing = QueueCellBenchRingInternal<64, QueueCellBenchInternal::kCapacity>;
    using PackedRing  = QueueCellBenchRingInternal<alignof( uintptr_t ), QueueCellBenchInternal::kCapacity>;

    const int64 alignedNanos = QueueCellBenchInternal::measureTransferNanos<AlignedRing>();
    const int64 packedNanos  = QueueCellBenchInternal::measureTransferNanos<PackedRing>();
    SW_EXPECT_TRUE( alignedNanos > 0 );
    SW_EXPECT_TRUE( packedNanos > 0 );

    const uint64 totalItems = static_cast<uint64>( QueueCellBenchInternal::kProducerCount ) * QueueCellBenchInternal::kPerProducer;
    SW_LOG_INFO_ID( "QueueCellBenchTest.CacheLineAlignedCellsVersusPacked",
                    "ring x%#, %# items, 4P/4C: aligned cell %# B -> %# ns (%# ns/item), packed cell %# B -> %# ns (%# ns/item), packed/aligned time x%#, ring bytes %# vs %#",
                    QueueCellBenchInternal::kCapacity, totalItems, sizeof( AlignedRing::Cell ), alignedNanos, alignedNanos / static_cast<int64>( totalItems ),
                    sizeof( PackedRing::Cell ), packedNanos, packedNanos / static_cast<int64>( totalItems ),
                    alignedNanos > 0 ? static_cast<float64>( packedNanos ) / static_cast<float64>( alignedNanos ) : 0.0, sizeof( AlignedRing::Cell ) * QueueCellBenchInternal::kCapacity,
                    sizeof( PackedRing::Cell ) * QueueCellBenchInternal::kCapacity );
}
