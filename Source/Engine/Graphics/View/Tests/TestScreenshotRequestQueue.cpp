#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/View/ScreenshotRequestQueue.h"

// ScreenshotRequestQueueTest — 스크린샷 우편함: 덮어쓴 요청은 하나로 세고, 처리하면 끝나지 않은 요청이 없다. 빈 경로는 받지 않는다. 패킷 칸은 하나다(nogpu).

using namespace sw;

/** @brief [ScreenshotRequestQueueTest] 꺼내기 전에 덮어쓴 요청은 하나로 세고, 처리를 알리면 끝나지 않은 요청이 없다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, OverwrittenRequestCountsOnce )
{
    ScreenshotRequestQueue queue;
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );
    queue.put( "a.png", RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    queue.put( "b.png", RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    SW_EXPECT_TRUE( queue.hasUnfinishedRequest() );

    ScreenshotRequest request;
    SW_REQUIRE_TRUE( queue.take( request ) );
    SW_EXPECT_EQUAL( string( "b.png" ), request._path );
    SW_EXPECT_FALSE( queue.take( request ) ); // 꺼낸 칸은 빈다
    queue.reportCompleted( true, request._path );
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );
    SW_EXPECT_EQUAL( 1u, queue.getCompletedSerial() );
    SW_EXPECT_EQUAL( string( "b.png" ), queue.getLastPath() );
    queue.reportCompleted( false, "c.png" );
    SW_EXPECT_TRUE( queue.getLastPath().empty() ); // 실패하면 마지막 경로가 빈다
}

/**
 * @brief [ScreenshotRequestQueueTest] 빈 경로 요청은 받지 않는다 — 앞 요청도 그대로다
 * @details 예전에는 빈 경로가 받은 수만 올리고 꺼낼 요청이 없어 `hasUnfinishedRequest` 가 영영 참이었고(기다리는 캡처가 멈춘다), 앞 요청을 빈 경로로 덮어 잃었다.
 */
SW_TEST_CASE( ScreenshotRequestQueueTest, EmptyPathIsRefused )
{
    ScreenshotRequestQueue queue;
    {
        SW_EXPECT_LOG( "Screenshot.EmptyPath" );
        queue.put( "", RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    }
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );

    queue.put( "a.png", RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    {
        SW_EXPECT_LOG( "Screenshot.EmptyPath" );
        queue.put( "", RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    }
    ScreenshotRequest request;
    SW_REQUIRE_TRUE( queue.take( request ) );
    SW_EXPECT_EQUAL( string( "a.png" ), request._path );
}

/** @brief [ScreenshotRequestQueueTest] 패킷 사진 칸은 하나다 — 차 있으면 다음 요청을 거절하고, 꺼내면 빈다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, PacketSlotHoldsOne )
{
    ScreenshotRequestQueue queue;
    SW_EXPECT_TRUE( queue.isPacketSlotFree() );
    SW_EXPECT_FALSE( queue.putForNextPacket( "" ) ); // 빈 경로는 받지 않는다
    SW_EXPECT_TRUE( queue.putForNextPacket( "p.png" ) );
    SW_EXPECT_FALSE( queue.putForNextPacket( "q.png" ) );
    SW_EXPECT_TRUE( queue.hasUnfinishedRequest() );
    string path;
    SW_REQUIRE_TRUE( queue.takeForPacket( path ) );
    SW_EXPECT_EQUAL( string( "p.png" ), path );
    SW_EXPECT_TRUE( queue.isPacketSlotFree() );
    SW_EXPECT_FALSE( queue.takeForPacket( path ) );
}
