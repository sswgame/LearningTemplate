// DedicatedWorker — 받은 순서대로 한 스레드에서 처리하고, stop 은 받은 일을 버리지 않고 모두 넘긴 뒤 합류하며, 그 뒤의 submit 은 거절하는지.
#include "pch.h"

#include "Core/Task/DedicatedWorker.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( DedicatedWorkerTest, ProcessesInOrderAndDrainsOnStop )
{
    using Worker = DedicatedWorker<int32>;
    vector<int32> listProcessed;
    Worker        worker;
    worker.start( "TestWorker", SW_DELEGATE_LAMBDA( Worker::ProcessDelegate, [&listProcessed]( int32& job, bool bStopping )
    {
        (void)bStopping; // 정지 뒤에 꺼낸 일도 처리 함수에 온다 — 여기서는 모두 센다
        listProcessed.push_back( job );
    } ) );

    constexpr int32 kJobCount = 200;
    for ( int32 index = 0; index < kJobCount; ++index )
    {
        int32 job = index;
        SW_EXPECT_TRUE( worker.submit( std::move( job ) ) );
    }
    worker.stop(); // 합류한 뒤라 listProcessed 를 이 스레드가 읽어도 된다

    SW_ASSERT_EQUAL( static_cast<size_t>( kJobCount ), listProcessed.size() );
    for ( int32 index = 0; index < kJobCount; ++index )
    {
        SW_EXPECT_EQUAL( index, listProcessed[static_cast<size_t>( index )] );
    }
    SW_EXPECT_EQUAL( 0u, worker.getQueuedCount() );

    int32 lateJob = 7;
    SW_EXPECT_FALSE( worker.submit( std::move( lateJob ) ) );
    worker.stop(); // 두 번 불러도 된다
}
