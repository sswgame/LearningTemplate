#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/View/ScreenshotRequestQueue.h"

// ScreenshotRequestQueueTest — 스크린샷 우편함: 덮어쓴 요청은 하나로 세고, 처리하면 끝나지 않은 요청이 없다. 패킷 칸은 하나다.
// 경로는 `AbsolutePath` 라 빈 경로 요청은 형식으로 만들 수 없다(빈 칸이 "요청 없음" 이다)(nogpu).

using namespace sw;

namespace
{
    /** @brief 케이스 폴더 아래 사진 경로입니다(케이스 폴더는 늘 절대 경로다). */
    AbsolutePath makeShotPath( string_view name )
    {
        return *AbsolutePath::tryMake( test::makeScratchPath( name ) );
    }
} // namespace

/** @brief [ScreenshotRequestQueueTest] 꺼내기 전에 덮어쓴 요청은 하나로 세고, 처리를 알리면 끝나지 않은 요청이 없다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, OverwrittenRequestCountsOnce )
{
    ScreenshotRequestQueue queue;
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );
    queue.put( makeShotPath( "a.png" ), RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    const AbsolutePath second = makeShotPath( "b.png" );
    queue.put( second, RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    SW_EXPECT_TRUE( queue.hasUnfinishedRequest() );

    ScreenshotRequest request;
    SW_REQUIRE_TRUE( queue.take( request ) );
    SW_EXPECT_EQUAL( second.str(), request._path );
    SW_EXPECT_FALSE( queue.take( request ) ); // 꺼낸 칸은 빈다
    queue.reportCompleted( true, request._path );
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );
    SW_EXPECT_EQUAL( 1u, queue.getCompletedSerial() );
    SW_EXPECT_EQUAL( second.str(), queue.getLastPath() );
    queue.reportCompleted( false, request._path );
    SW_EXPECT_TRUE( queue.getLastPath().empty() ); // 실패하면 마지막 경로가 빈다
}

/** @brief [ScreenshotRequestQueueTest] 빈 경로는 요청 경로 형식(`AbsolutePath`)이 되지 못한다 — 우편함에 "요청 없음" 과 같은 빈 요청이 들어갈 길이 없다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, EmptyPathCannotBecomeARequest )
{
    SW_EXPECT_FALSE( AbsolutePath::tryMake( "" ).has_value() );
    SW_EXPECT_FALSE( AbsolutePath::tryMake( "shot.png" ).has_value() ); // 작업 폴더 기준 상대 경로도 아니다
}

/** @brief [ScreenshotRequestQueueTest] 패킷 사진 칸은 하나다 — 차 있으면 다음 요청을 거절하고, 꺼내면 빈다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, PacketSlotHoldsOne )
{
    ScreenshotRequestQueue queue;
    SW_EXPECT_TRUE( queue.isPacketSlotFree() );
    const AbsolutePath first = makeShotPath( "p.png" );
    SW_EXPECT_TRUE( queue.putForNextPacket( first ) );
    SW_EXPECT_FALSE( queue.putForNextPacket( makeShotPath( "q.png" ) ) );
    SW_EXPECT_TRUE( queue.hasUnfinishedRequest() );
    string path;
    SW_REQUIRE_TRUE( queue.takeForPacket( path ) );
    SW_EXPECT_EQUAL( first.str(), path );
    SW_EXPECT_TRUE( queue.isPacketSlotFree() );
    SW_EXPECT_FALSE( queue.takeForPacket( path ) );
}
