#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/View/ScreenshotRequestQueue.h"

// ScreenshotRequestQueueTest — 스크린샷 우편함: 요청은 받은 차례의 줄이고 표마다 제 결과를 읽는다. 처리하면 끝나지 않은 요청이 없다. 패킷 칸은 하나다.
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

/** @brief [ScreenshotRequestQueueTest] 요청은 받은 차례로 꺼내고, 표마다 제 결과를 읽는다 — 두 쪽이 함께 요청해도 남의 결과를 제 것으로 읽지 않는다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, EachTicketReadsItsOwnResult )
{
    ScreenshotRequestQueue queue;
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );
    const AbsolutePath first   = makeShotPath( "a.png" );
    const AbsolutePath second  = makeShotPath( "b.png" );
    const uint32       ticketA = queue.put( first, RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    const uint32       ticketB = queue.put( second, RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    SW_EXPECT_NOT_EQUAL( ticketA, ticketB );
    SW_EXPECT_TRUE( queue.hasUnfinishedRequest() );
    SW_EXPECT_TRUE( queue.findResult( ticketA ) == ScreenshotTicketState::Pending );

    ScreenshotRequest request;
    SW_REQUIRE_TRUE( queue.take( request ) );
    SW_EXPECT_EQUAL( first.str(), request._path ); // 덮어쓰지 않고 받은 차례다
    SW_EXPECT_EQUAL( ticketA, request._ticket );
    queue.reportCompleted( request._ticket, false, request._path );
    SW_EXPECT_TRUE( queue.findResult( ticketA ) == ScreenshotTicketState::Failed );
    SW_EXPECT_TRUE( queue.findResult( ticketB ) == ScreenshotTicketState::Pending );
    SW_EXPECT_TRUE( queue.getLastPath().empty() ); // 실패하면 마지막 경로가 빈다

    SW_REQUIRE_TRUE( queue.take( request ) );
    queue.reportCompleted( ticketB, true, request._path );
    SW_EXPECT_TRUE( queue.findResult( ticketB ) == ScreenshotTicketState::Written );
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );
    SW_EXPECT_EQUAL( 2u, queue.getCompletedSerial() );
    SW_EXPECT_EQUAL( second.str(), queue.getLastPath() );
    SW_EXPECT_TRUE( queue.findResult( 0 ) == ScreenshotTicketState::Unknown );
    SW_EXPECT_TRUE( queue.findResult( ticketB + 1 ) == ScreenshotTicketState::Unknown ); // 내준 적 없는 표
}

/** @brief [ScreenshotRequestQueueTest] 줄이 차면 새 요청은 받지 않고 그 표는 곧바로 실패다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, FullQueueFailsTheNewTicket )
{
    SW_EXPECT_LOG( "Screenshot.QueueFull" );
    ScreenshotRequestQueue queue;
    const AbsolutePath     path = makeShotPath( "full.png" );
    for ( uint32 index = 0; index < ScreenshotRequestQueue::kMaxPendingCount; ++index )
    {
        (void)queue.put( path, RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    }
    const uint32 overflow = queue.put( path, RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    SW_EXPECT_TRUE( queue.findResult( overflow ) == ScreenshotTicketState::Failed );
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

/** @brief [ScreenshotRequestQueueTest] 앞 요청의 결과는 뒤 요청이 끝난 뒤에도 제 표로 읽힌다 — 에디터 버튼과 시작 인자 사진이 겹쳐도 남의 경로를 제 결과로 읽지 않는다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, EarlierTicketSurvivesLaterResults )
{
    ScreenshotRequestQueue queue;
    const uint32           editorTicket  = queue.put( makeShotPath( "editor.png" ), RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    const uint32           startupTicket = queue.put( makeShotPath( "startup.png" ), RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    ScreenshotRequest      request;
    SW_REQUIRE_TRUE( queue.take( request ) );
    queue.reportCompleted( request._ticket, true, request._path );
    SW_REQUIRE_TRUE( queue.take( request ) );
    queue.reportCompleted( request._ticket, false, request._path ); // 뒤 요청은 실패했다 — 마지막 경로가 빈다
    SW_EXPECT_TRUE( queue.getLastPath().empty() );
    SW_EXPECT_TRUE( queue.findResult( editorTicket ) == ScreenshotTicketState::Written );
    SW_EXPECT_TRUE( queue.findResult( startupTicket ) == ScreenshotTicketState::Failed );
}
