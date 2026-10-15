/**
 * @file TestDeferredImageUnloadQueue.cpp
 * @brief DeferredImageUnloadQueue — 교체된 옛 이미지를 상한만큼의 배치로 남기고, 넘치면 가장 오래된 배치를 통째로 비우는지.
 * @details 실제 언로드는 SmokeTest `DeferredUnloadImagesStayMappedUntilTheirBatchIsEvicted` 가 DLL 로 본다. 여기서는 큐의 배치 셈만 본다 —
 *          모든 이미지를 "구독 중인 채널을 만든 이미지"(`bKeepMapped`)로 넣어 가짜 핸들을 언로드하지 않게 한다. 그런 이미지도 큐에서는 빠진다.
 */
#include "pch.h"

#include "ModuleHost/DeferredImageUnloadQueue.h"

#include "TestFramework/TestFramework.h"

SW_TEST_CASE( DeferredImageUnloadQueueTest, OldestBatchLeavesWhenTheLimitIsExceeded )
{
    constexpr uint32 kMaxBatchCount = 2;
    int32            arrFakeImage[4]{};

    sw::DeferredImageUnloadQueue queue{ kMaxBatchCount };
    SW_EXPECT_EQUAL( 0u, queue.getImageCount() );

    // 핸들이 없으면(첫 로드라 바꾼 이미지가 없다) 넣지 않는다.
    queue.beginBatch();
    queue.deferUnload( "SWGame", nullptr, "", true );
    SW_EXPECT_EQUAL( 0u, queue.getImageCount() );

    // 배치 하나에 이미지 둘(키트와 그것에 의존하는 게임이 함께 바뀐 연쇄).
    queue.deferUnload( "Kit", &arrFakeImage[0], "", true );
    queue.deferUnload( "SWGame", &arrFakeImage[1], "", true );
    SW_EXPECT_EQUAL( 2u, queue.getImageCount() );

    queue.beginBatch();
    queue.deferUnload( "SWGame", &arrFakeImage[2], "", true );
    SW_EXPECT_EQUAL( 3u, queue.getImageCount() );

    // 세 번째 배치가 들어오면 상한(2)을 넘으므로 첫 배치(이미지 둘)가 통째로 빠진다.
    queue.beginBatch();
    queue.deferUnload( "SWGame", &arrFakeImage[3], "", true );
    SW_EXPECT_EQUAL( 2u, queue.getImageCount() );

    queue.unloadOldestBatch();
    SW_EXPECT_EQUAL( 1u, queue.getImageCount() );

    queue.unloadAll();
    SW_EXPECT_EQUAL( 0u, queue.getImageCount() );
}
