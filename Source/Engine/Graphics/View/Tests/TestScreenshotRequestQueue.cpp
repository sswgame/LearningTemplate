#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/View/ScreenshotRequestQueue.h"

// ScreenshotRequestQueueTest — 스크린샷 우편함: 요청은 받은 차례의 줄이고 표마다 제 결과를 읽는다. 처리하면 끝나지 않은 요청이 없다. 패킷 칸은 하나다.
// 경로는 `AbsolutePath` 라 빈 경로 요청은 형식으로 만들 수 없다(빈 칸이 "요청 없음" 이다)(nogpu).

using namespace sw;

namespace
{
    /** @brief 케이스 폴더 아래 사진 경로입니다(케이스 폴더는 늘 절대 경로다). */
    AbsolutePath makeShotPath( string_view name )
    {
        return *AbsolutePath::tryMake( test::makeScratchPath( name ).str() );
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
    SW_EXPECT_TRUE( first == request._path ); // 덮어쓰지 않고 받은 차례다
    SW_EXPECT_EQUAL( ticketA, request._ticket );
    queue.reportCompleted( request._ticket, false );
    SW_EXPECT_TRUE( queue.findResult( ticketA ) == ScreenshotTicketState::Failed );
    SW_EXPECT_TRUE( queue.findResult( ticketB ) == ScreenshotTicketState::Pending );

    SW_REQUIRE_TRUE( queue.take( request ) );
    queue.reportCompleted( ticketB, true );
    SW_EXPECT_TRUE( queue.findResult( ticketB ) == ScreenshotTicketState::Written );
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );
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
    AbsolutePath path;
    SW_REQUIRE_TRUE( queue.takeForPacket( path ) );
    SW_EXPECT_TRUE( first == path );
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
    queue.reportCompleted( request._ticket, true );
    SW_REQUIRE_TRUE( queue.take( request ) );
    queue.reportCompleted( request._ticket, false ); // 뒤 요청은 실패했다
    SW_EXPECT_TRUE( queue.findResult( editorTicket ) == ScreenshotTicketState::Written );
    SW_EXPECT_TRUE( queue.findResult( startupTicket ) == ScreenshotTicketState::Failed );
}

/** @brief [ScreenshotRequestQueueTest] 바이트 요청은 이름이 있어야 하고, 결과는 제 표로 한 번만 받는다 — 뒤에 끝난 다른 요청(파일 · 바이트)이 앞 표의 결과를 덮지 않는다 */
SW_TEST_CASE( ScreenshotRequestQueueTest, PNGBytesAreTakenOnceByTicket )
{
    ScreenshotRequestQueue queue;
    SW_EXPECT_LOG( "ScreenshotRequestQueue.EmptyByteRequestName" ); // 이름 없는 요청은 오류로 거절한다
    SW_EXPECT_EQUAL( 0u, queue.putForPNGBytes( "", RHITextureHandle{ 7 }, RHIFormat::R8G8B8A8_UNORM ) );
    SW_EXPECT_FALSE( queue.hasUnfinishedRequest() );

    const uint32 ticketA = queue.putForPNGBytes( "thumb.a", RHITextureHandle{ 7 }, RHIFormat::R8G8B8A8_UNORM );
    const uint32 ticketB = queue.putForPNGBytes( "thumb.b", RHITextureHandle{ 7 }, RHIFormat::R8G8B8A8_UNORM );
    const uint32 ticketC = queue.put( makeShotPath( "c.png" ), RHITextureHandle{}, RHIFormat::R8G8B8A8_UNORM );
    SW_REQUIRE_TRUE( ticketA != 0 && ticketB != 0 );
    ScreenshotRequest request;
    SW_REQUIRE_TRUE( queue.take( request ) );
    SW_EXPECT_TRUE( request._output == ScreenshotOutput::PNGBytes );
    SW_EXPECT_EQUAL( string{ "thumb.a" }, request._name );
    SW_EXPECT_TRUE( request._path.empty() );
    SW_EXPECT_EQUAL( ticketA, request._ticket );
    queue.reportCompletedPNGBytes( request._ticket, true, vector<uint8>{ 1, 2, 3 } );
    SW_REQUIRE_TRUE( queue.take( request ) );
    queue.reportCompletedPNGBytes( request._ticket, true, vector<uint8>{ 4 } );
    SW_REQUIRE_TRUE( queue.take( request ) );
    SW_EXPECT_TRUE( request._output == ScreenshotOutput::File );
    queue.reportCompleted( request._ticket, true );

    vector<uint8> bytes;
    SW_EXPECT_FALSE( queue.takePNGBytes( ticketC, bytes ) ); // 파일 요청 표에는 바이트가 없다
    SW_REQUIRE_TRUE( queue.takePNGBytes( ticketA, bytes ) ); // 뒤 요청이 끝났어도 제 표로 받는다
    SW_EXPECT_TRUE( bytes.size() == 3 );
    SW_EXPECT_FALSE( queue.takePNGBytes( ticketA, bytes ) ); // 한 번만
    SW_REQUIRE_TRUE( queue.takePNGBytes( ticketB, bytes ) );
    SW_EXPECT_TRUE( bytes.size() == 1 );
    SW_EXPECT_TRUE( queue.findResult( ticketA ) == ScreenshotTicketState::Written );
}
