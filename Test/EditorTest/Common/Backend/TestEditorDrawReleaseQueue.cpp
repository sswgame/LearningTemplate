#include "pch.h"

#include "Editor/Common/Backend/EditorDrawReleaseQueue.h"

#include "EngineTest/RHIFakeDevice.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 불린 횟수를 세는 해제 콜백을 만듭니다. */
    RHIResourceReleaseDelegate makeCountingRelease( uint32& outCallCount )
    {
        uint32* pCallCount = &outCallCount;
        return SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pCallCount]()
        { ++( *pCallCount ); } );
    }

    /** @brief 가짜 디바이스가 받아 둔 해제를 "GPU 가 그 프레임을 끝냈다" 로 보고 모두 부릅니다. */
    void completeGpuFrames( test::FakeRHIDevice& device )
    {
        for ( const RHIResourceReleaseDelegate& releaseDelegate : device._listGpuRelease )
        {
            releaseDelegate();
        }
        device._listGpuRelease.clear();
    }
} // namespace

/**
 * @brief [EditorDrawReleaseQueueTest] 놓인 자원은 그 뒤에 낸 스냅샷을 그리는 프레임이 올 때까지 디바이스로 넘어가지 않는다
 * @details 렌더 스레드는 UI 가 새 스냅샷을 내기 전까지 옛 스냅샷을 여러 프레임에 다시 그린다. 그 프레임들이 놓인 디스크립터를 쓰므로,
 *          옛 스냅샷을 그리는 프레임에서 넘기면 그 뒤 프레임이 놓인 세트를 쓴다(Vulkan "vkFreeDescriptorSets: in use").
 */
SW_TEST_CASE( EditorDrawReleaseQueueTest, ReleaseWaitsForTheFirstSnapshotPublishedAfterIt )
{
    EditorDrawReleaseQueue queue;
    test::FakeRHIDevice    device;
    uint32                 callCount{ 0 };

    queue.markSnapshotPublished( 1 );
    queue.enqueue( makeCountingRelease( callCount ) );

    // 스냅샷 1 은 놓기 전에 냈으니 그 자원을 그릴 수 있다. 몇 번을 다시 그려도 넘기지 않는다.
    SW_EXPECT_EQUAL( 0u, queue.handOverToDevice( device, 1 ) );
    SW_EXPECT_EQUAL( 0u, queue.handOverToDevice( device, 1 ) );
    SW_EXPECT_TRUE( device._listGpuRelease.empty() );

    queue.markSnapshotPublished( 2 );
    SW_EXPECT_EQUAL( 0u, queue.handOverToDevice( device, 1 ) );
    SW_EXPECT_EQUAL( 1u, queue.getPendingCount() );

    // 스냅샷 2 를 그리는 프레임에서 넘긴다. 부르는 것은 디바이스(그 프레임의 GPU 완료 뒤)다.
    SW_EXPECT_EQUAL( 1u, queue.handOverToDevice( device, 2 ) );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( device._listGpuRelease.size() ) );
    SW_EXPECT_EQUAL( 0u, callCount );

    completeGpuFrames( device );
    SW_EXPECT_EQUAL( 1u, callCount );
}

/**
 * @brief [EditorDrawReleaseQueueTest] 스냅샷을 내기 전에 놓인 자원은 첫 스냅샷부터 넘어가고, 그린 스냅샷이 없는 프레임(번호 0)은 넘기지 않는다
 */
SW_TEST_CASE( EditorDrawReleaseQueueTest, ReleaseBeforeAnySnapshotGoesWithTheFirstOne )
{
    EditorDrawReleaseQueue queue;
    test::FakeRHIDevice    device;
    uint32                 callCount{ 0 };

    queue.enqueue( makeCountingRelease( callCount ) );
    SW_EXPECT_EQUAL( 0u, queue.handOverToDevice( device, 0 ) );

    queue.markSnapshotPublished( 1 );
    SW_EXPECT_EQUAL( 1u, queue.handOverToDevice( device, 1 ) );
    completeGpuFrames( device );
    SW_EXPECT_EQUAL( 1u, callCount );
}

/**
 * @brief [EditorDrawReleaseQueueTest] 여러 UI 프레임에 걸쳐 놓인 자원은 각자 자기 뒤의 스냅샷에서 넘어간다
 */
SW_TEST_CASE( EditorDrawReleaseQueueTest, EachReleaseWaitsForItsOwnSnapshot )
{
    EditorDrawReleaseQueue queue;
    test::FakeRHIDevice    device;
    uint32                 firstCount{ 0 };
    uint32                 secondCount{ 0 };

    queue.markSnapshotPublished( 4 );
    queue.enqueue( makeCountingRelease( firstCount ) ); // 스냅샷 5 부터 안 그린다
    queue.markSnapshotPublished( 5 );
    queue.enqueue( makeCountingRelease( secondCount ) ); // 스냅샷 6 부터 안 그린다
    queue.markSnapshotPublished( 6 );

    SW_EXPECT_EQUAL( 1u, queue.handOverToDevice( device, 5 ) );
    completeGpuFrames( device );
    SW_EXPECT_EQUAL( 1u, firstCount );
    SW_EXPECT_EQUAL( 0u, secondCount );

    SW_EXPECT_EQUAL( 1u, queue.handOverToDevice( device, 6 ) );
    completeGpuFrames( device );
    SW_EXPECT_EQUAL( 1u, secondCount );
}

/**
 * @brief [EditorDrawReleaseQueueTest] 종료 때 flushAll 이 넘기지 않은 해제를 모두 부른다(모듈이 내려가기 전에 비워야 한다)
 */
SW_TEST_CASE( EditorDrawReleaseQueueTest, FlushAllRunsWhatWasNotHandedOver )
{
    EditorDrawReleaseQueue queue;
    uint32                 callCount{ 0 };

    queue.markSnapshotPublished( 1 );
    queue.enqueue( makeCountingRelease( callCount ) );
    queue.enqueue( makeCountingRelease( callCount ) );
    SW_EXPECT_EQUAL( 2u, queue.getPendingCount() );

    queue.flushAll();
    SW_EXPECT_EQUAL( 2u, callCount );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
}
