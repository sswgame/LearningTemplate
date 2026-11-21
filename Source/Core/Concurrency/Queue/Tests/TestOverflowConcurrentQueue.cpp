/**
 * @file TestOverflowConcurrentQueue.cpp
 * @brief OverflowConcurrentQueue — 링 용량을 넘겨도 넣기가 실패하지 않고, 한 생산자의 차례(FIFO)가 링 → 넘침 경계에서도 지켜지며, 여러 스레드가 넣고 꺼내도 잃는 것이 없다.
 */
#include "pch.h"

#include "Core/Concurrency/Queue/OverflowConcurrentQueue.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Testing/TestFramework.h"

#include <thread>

/**
 * @brief [OverflowConcurrentQueueTest] 링 용량의 세 배를 넣어도 모두 들어가고, 한 생산자가 넣은 차례대로 나온다
 */
SW_TEST_CASE( OverflowConcurrentQueueTest, PastRingCapacityKeepsEveryItemInOrder )
{
    sw::OverflowConcurrentQueue<uint32, 16> queue;
    constexpr uint32                        kCount = 16 * 3;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        queue.enqueue( index );
    }
    SW_EXPECT_TRUE( queue.getOverflowCount() > 0u );
    SW_EXPECT_FALSE( queue.empty() );

    uint32 expected = 0;
    uint32 item     = 0;
    while ( queue.dequeue( item ) )
    {
        SW_EXPECT_EQUAL( expected, item );
        ++expected;
    }
    SW_EXPECT_EQUAL( kCount, expected );
    SW_EXPECT_TRUE( queue.empty() );
    SW_EXPECT_EQUAL( 0u, queue.getOverflowCount() );

    // 넘침을 다 비운 뒤에는 다시 링으로 들어간다
    queue.enqueue( 7u );
    SW_EXPECT_EQUAL( 0u, queue.getOverflowCount() );
    SW_REQUIRE_TRUE( queue.dequeue( item ) );
    SW_EXPECT_EQUAL( 7u, item );
}

/**
 * @brief [OverflowConcurrentQueueTest] 여러 생산자 · 소비자가 링보다 훨씬 많이 넣고 꺼내도 모든 항목이 정확히 한 번 나온다
 */
SW_TEST_CASE( OverflowConcurrentQueueTest, ConcurrentProducersAndConsumersLoseNothing )
{
    sw::OverflowConcurrentQueue<uint32, 64> queue;
    constexpr uint32                        kProducerCount = 4;
    constexpr uint32                        kPerProducer   = 5000;
    constexpr uint32                        kTotal         = kProducerCount * kPerProducer;
    sw::vector<sw::atomic<uint32>>          listSeen( kTotal );
    sw::atomic<uint32>                      takenCount{ 0 };
    sw::atomic<uint32>* const               pSeen = listSeen.data(); // 스레드 전에 한 번 — vector 의 non-const 접근은 경쟁 감지에서 쓰기로 센다(원소는 atomic 이다)

    sw::vector<std::thread> listThread;
    for ( uint32 producer = 0; producer < kProducerCount; ++producer )
    {
        listThread.emplace_back( [&queue, producer]()
        {
            for ( uint32 index = 0; index < kPerProducer; ++index )
            {
                queue.enqueue( producer * kPerProducer + index );
            }
        } );
    }
    for ( uint32 consumer = 0; consumer < 2; ++consumer )
    {
        listThread.emplace_back( [&queue, pSeen, &takenCount]()
        {
            uint32 item = 0;
            while ( takenCount.load( std::memory_order_acquire ) < kTotal )
            {
                if ( queue.dequeue( item ) )
                {
                    pSeen[item].fetch_add( 1, std::memory_order_relaxed );
                    takenCount.fetch_add( 1, std::memory_order_acq_rel );
                }
                else
                    std::this_thread::yield();
            }
        } );
    }
    for ( std::thread& thread : listThread )
    {
        thread.join();
    }

    uint32 wrongCount = 0;
    for ( uint32 index = 0; index < kTotal; ++index )
    {
        if ( listSeen[index].load() != 1u )
            ++wrongCount;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );
    SW_EXPECT_TRUE( queue.empty() );
}
