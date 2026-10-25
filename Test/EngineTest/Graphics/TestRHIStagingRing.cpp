#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIStagingAllocator.h"

#include "TestFramework/TestFramework.h"

// 업로드 · 되읽기 링(RHIStagingAllocator)의 장부 규칙을 GPU 없이 본다. 그 링을 쓰는 GL 업로드 · 되읽기는 TestRHIStagingRingGPU.cpp 가 실제 디바이스로 본다.

namespace
{
    /** @brief 할당 결과가 `Allocated` 인지 묻습니다(열거값을 테스트 메시지 없이 비교하는 줄을 줄인다). */
    bool isAllocated( sw::RHIStagingAllocateResult result )
    {
        return result == sw::RHIStagingAllocateResult::Allocated;
    }
} // namespace

/**
 * @brief [RHIStagingRingTest] 구간은 정렬된 오프셋에 차례로 잡히고, 끝에 자리가 없으면 돌아온 앞자리만큼만 다시 쓴다
 * @details 링보다 큰 요청과 0 바이트는 기다려도 못 받으므로 `TooLarge`(제출의 Rejected), 지금 자리가 없을 뿐이면 `Full`(Busy)이다.
 */
SW_TEST_CASE( RHIStagingRingTest, AllocatesAlignedSpansAndWrapsAfterTheFrontCompletes )
{
    sw::RHIStagingAllocator ring;
    ring.initialize( 1024, 256, 0 );

    sw::RHIStagingSpan a{};
    sw::RHIStagingSpan b{};
    sw::RHIStagingSpan c{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 100, 0, a ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 300, 0, b ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 200, 0, c ) ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), a._offsetBytes );
    SW_EXPECT_EQUAL( static_cast<uint64>( 256 ), b._offsetBytes ); // 100 바이트 뒤가 아니라 다음 256 경계
    SW_EXPECT_EQUAL( static_cast<uint64>( 768 ), c._offsetBytes );

    sw::RHIStagingSpan rejected{};
    SW_EXPECT_TRUE( ring.allocate( 300, 0, rejected ) == sw::RHIStagingAllocateResult::Full ); // 끝에도 앞에도 자리가 없다
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), rejected._sizeBytes );                          // 못 잡은 구간은 빈 값이다
    SW_EXPECT_TRUE( ring.allocate( 1025, 0, rejected ) == sw::RHIStagingAllocateResult::TooLarge );
    SW_EXPECT_TRUE( ring.allocate( 0, 0, rejected ) == sw::RHIStagingAllocateResult::TooLarge );

    ring.markSubmitted( a, 1 );
    ring.markSubmitted( b, 2 );
    ring.markSubmitted( c, 3 );

    // 워터마크 1: a 만 돌아온다. 빈 곳은 [0, 256) 이라 300 은 못 받고 200 은 그 자리에 받는다.
    sw::RHIStagingSpan wrapped{};
    SW_EXPECT_TRUE( ring.allocate( 300, 1, rejected ) == sw::RHIStagingAllocateResult::Full );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 200, 1, wrapped ) ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), wrapped._offsetBytes );
    // 이제 빈 곳이 없다 — 200 바이트 구간도 정렬 단위(256) 한 칸을 다 쓴다.
    SW_EXPECT_TRUE( ring.allocate( 16, 1, rejected ) == sw::RHIStagingAllocateResult::Full );

    // 워터마크 3: b · c 가 돌아오고, 아직 제출하지 않은 구간(시리얼 0)은 남는다.
    sw::RHIStagingSpan afterWrap{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 700, 3, afterWrap ) ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 256 ), afterWrap._offsetBytes );
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), ring.getLiveCount() );
}

/**
 * @brief [RHIStagingRingTest] 구간은 자기 시리얼이 워터마크를 지나면 할당 순서와 무관하게 돌아오고, 아직 끝나지 않은 구간은 덮지 않는다
 * @details 시리얼은 컨텍스트 · 큐 락 안에서 매기므로 먼저 잡은 스레드가 늦게 제출할 수 있다. 워터마크는 연속으로 끝난 시리얼의 끝이라 뒤 구간이 먼저 돌아와도
 *          GPU 가 그 구간을 다 쓴 것이 보장된다. 제출 전(복사 중) 구간은 자기 자리만 쥐고 다른 구간의 회수를 막지 않으며, 놓으면(`free`) 시리얼 없이 돌아온다.
 */
SW_TEST_CASE( RHIStagingRingTest, ReturnsEachSpanWhenItsOwnSerialCompletes )
{
    sw::RHIStagingAllocator ring;
    ring.initialize( 512, 16, 0 );

    sw::RHIStagingSpan first{};
    sw::RHIStagingSpan second{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, first ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, second ) ) );
    ring.markSubmitted( second, 1 ); // 뒤에 잡은 쪽이 먼저 제출했다
    ring.markSubmitted( first, 2 );

    sw::RHIStagingSpan probe{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 1, probe ) ) ); // second 가 끝났다 — 앞의 first 가 아직이어도 그 자리가 돌아온다
    SW_EXPECT_EQUAL( second._offsetBytes, probe._offsetBytes );
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), ring.getLiveCount() );
    sw::RHIStagingSpan extra{};
    SW_EXPECT_TRUE( ring.allocate( 16, 1, extra ) == sw::RHIStagingAllocateResult::Full ); // first 는 GPU 가 아직 읽는다 — 덮지 않는다

    ring.free( probe );
    ring.freeCompleted( 2 );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), ring.getLiveCount() );

    sw::RHIStagingSpan unsubmitted{};
    sw::RHIStagingSpan submitted{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 128, 2, unsubmitted ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 128, 2, submitted ) ) );
    ring.markSubmitted( submitted, 5 );
    ring.freeCompleted( 10 );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() ); // 제출 전(복사 중) 구간만 남고, 뒤에 잡은 submitted 는 돌아왔다
    ring.free( unsubmitted );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), ring.getLiveCount() );

    ring.shutdown();
    SW_EXPECT_TRUE( ring.allocate( 16, 0, probe ) == sw::RHIStagingAllocateResult::TooLarge ); // 용량 0 이면 모두 못 받는다
}

/**
 * @brief [RHIStagingRingTest] 가져가지 않은 되읽기 하나는 자기 자리만 쥔다 — 그 뒤의 읽기는 링 용량을 몇 바퀴 넘겨도 계속 받고, 중간에 빈 자리도 다시 쓴다
 * @details 티켓을 쥔 스레드가 `readbackTexture2DAndWait` 를 부르는 모양이다. 할당 순서로만 돌려받는 장부라면 쥔 구간이 링 맨 앞을 막아 그 읽기가 시간 초과까지 `Busy` 다.
 *          쥔 구간들이 남은 자리를 모두 차지하면 그때만 `Full` 이다.
 */
SW_TEST_CASE( RHIStagingRingTest, AnUntakenReadbackPinsOnlyItsOwnBytes )
{
    sw::RHIStagingAllocator ring;
    ring.initialize( 1024, 256, 0 );

    sw::RHIStagingSpan held{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, held ) ) ); // 가져가지 않고 쥐는 읽기
    for ( uint32 readIndex = 0; readIndex < 16; ++readIndex )
    {
        sw::RHIStagingSpan blocking{};
        SW_ASSERT_TRUE( isAllocated( ring.allocate( 768, 0, blocking ) ) ); // 남은 자리 전부 — 16 번이면 링 용량을 열두 바퀴 넘는다
        SW_EXPECT_TRUE( blocking._offsetBytes != held._offsetBytes );
        ring.free( blocking ); // 가져갔다
    }

    sw::RHIStagingSpan a{};
    sw::RHIStagingSpan b{};
    sw::RHIStagingSpan c{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, a ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, b ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, c ) ) );
    sw::RHIStagingSpan probe{};
    SW_EXPECT_TRUE( ring.allocate( 16, 0, probe ) == sw::RHIStagingAllocateResult::Full ); // 쥔 구간이 자리를 다 차지했다

    ring.free( b ); // 가운데를 가져갔다 — 앞뒤(a · c)가 남아 있어도 그 자리가 돌아온다
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, probe ) ) );
    SW_EXPECT_EQUAL( b._offsetBytes, probe._offsetBytes );
    SW_EXPECT_EQUAL( static_cast<uint32>( 4 ), ring.getLiveCount() );
}

/**
 * @brief [RHIStagingRingTest] 되읽기 링의 구간은 GPU 가 끝나도 돌아오지 않고, CPU 가 가져갈 때(`free`)나 버린 읽기의 펜스가 지날 때(`markSubmitted`) 돌아온다
 * @details GL 되읽기는 제출 때 시리얼을 붙이지 않는다 — 워터마크만 보고 돌려받으면 아직 복사하지 않은 결과를 다음 읽기가 덮는다.
 */
SW_TEST_CASE( RHIStagingRingTest, ReadbackSpansReturnWhenTakenOrWhenTheDiscardedReadFinishes )
{
    sw::RHIStagingAllocator ring;
    ring.initialize( 512, 256, 0 );

    sw::RHIStagingSpan taken{};
    sw::RHIStagingSpan discarded{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, taken ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, discarded ) ) );

    sw::RHIStagingSpan probe{};
    SW_EXPECT_TRUE( ring.allocate( 16, 100, probe ) == sw::RHIStagingAllocateResult::Full ); // 워터마크가 한참 지나도 가져가지 않은 구간은 남는다(Busy)

    ring.markSubmitted( discarded, 7 ); // 뒤 구간을 버렸다 — GPU 가 그 구간에 다 쓴 뒤(펜스 7)에 돌아온다
    ring.freeCompleted( 6 );
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), ring.getLiveCount() );
    ring.freeCompleted( 7 );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() ); // 앞의 읽기를 아직 가져가지 않았어도 버린 구간은 돌아온다

    ring.free( taken ); // CPU 가 결과를 복사했다
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), ring.getLiveCount() );
}

/**
 * @brief [RHIStagingRingTest] 링보다 큰 한 장은 링이 비었을 때만 단독 구간으로 받고, 그동안 다른 요청은 Full, 단독 상한을 넘으면 TooLarge 다
 * @details 링이 비지 않아 Full 을 받은 단독 요청은 예약을 남겨 새 링 요청도 Full 로 돌린다(작은 업로드가 끊이지 않아 큰 한 장이 굶지 않게).
 *          단독 구간은 링 구간과 같이 시리얼이 워터마크를 지나면 돌아오고, 돌아오면 링 요청을 다시 받는다.
 */
SW_TEST_CASE( RHIStagingRingTest, ASingleRequestLargerThanTheRingWaitsForAnEmptyRing )
{
    constexpr int64         kLongReservationMicroseconds = 60ll * 1000 * 1000; // 테스트가 도는 동안 끝나지 않는 예약
    sw::RHIStagingAllocator ring;
    ring.initialize( 1024, 256, 4096, kLongReservationMicroseconds );

    sw::RHIStagingSpan small{};
    sw::RHIStagingSpan probe{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, small ) ) );
    SW_EXPECT_TRUE( ring.allocate( 2048, 0, probe ) == sw::RHIStagingAllocateResult::Full );     // 진행 중인 구간이 있다 — 기다리면 받는다
    SW_EXPECT_TRUE( ring.allocate( 16, 0, probe ) == sw::RHIStagingAllocateResult::Full );       // 단독 요청이 기다린다 — 자리가 있어도 새 링 요청은 받지 않는다
    SW_EXPECT_TRUE( ring.allocate( 4097, 0, probe ) == sw::RHIStagingAllocateResult::TooLarge ); // 단독 상한을 넘으면 기다려도 못 받는다

    ring.markSubmitted( small, 1 );
    sw::RHIStagingSpan solo{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 2048, 1, solo ) ) ); // 워터마크 1 — 링이 비었다
    SW_EXPECT_TRUE( solo._bSolo != SW_FALSE );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), solo._offsetBytes );
    SW_EXPECT_EQUAL( static_cast<uint64>( 2048 ), solo._sizeBytes );
    SW_EXPECT_TRUE( ring.hasLiveSolo() );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() );

    SW_EXPECT_TRUE( ring.allocate( 16, 1, probe ) == sw::RHIStagingAllocateResult::Full );   // 단독 구간이 사는 동안은 링 요청도
    SW_EXPECT_TRUE( ring.allocate( 4096, 1, probe ) == sw::RHIStagingAllocateResult::Full ); // 다른 단독 요청도 받지 않는다 — 최고점은 max(링, 한 장)

    ring.markSubmitted( solo, 2 );
    ring.freeCompleted( 1 );
    SW_EXPECT_TRUE( ring.hasLiveSolo() );                             // 아직 GPU 가 읽는다
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 1024, 2, probe ) ) ); // 워터마크 2 — 단독 구간이 돌아오고 링 전체를 다시 받는다(예약은 단독 구간을 내줄 때 끝났다)
    SW_EXPECT_TRUE( probe._bSolo == SW_FALSE );
    SW_EXPECT_TRUE( ring.hasLiveSolo() == false );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() );
}

/**
 * @brief [RHIStagingRingTest] 다시 오지 않는 단독 요청의 예약은 끝나 링 요청을 다시 받고, `free` 한 단독 구간은 바로 돌아오며, 단독 상한 0 이면 링보다 큰 한 장은 TooLarge 다
 * @details 예약 시간 0 은 포기한 단독 요청이 예약 시간을 다 쓴 뒤와 같다. 되읽기는 단독 구간도 CPU 가 가져갈 때(`free`) 돌아온다.
 */
SW_TEST_CASE( RHIStagingRingTest, AnAbandonedSingleRequestStopsBlockingTheRing )
{
    sw::RHIStagingAllocator ring;
    ring.initialize( 1024, 256, 4096, 0 );

    sw::RHIStagingSpan held{};
    sw::RHIStagingSpan probe{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, held ) ) ); // 가져가지 않은 되읽기 — 링이 비지 않는다
    SW_EXPECT_TRUE( ring.allocate( 2048, 0, probe ) == sw::RHIStagingAllocateResult::Full );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, probe ) ) ); // 예약이 끝났다 — 단독 요청이 다시 오지 않으면 링 요청이 막히지 않는다
    ring.free( probe );
    ring.free( held );

    sw::RHIStagingSpan solo{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 3000, 0, solo ) ) );
    ring.free( solo ); // 가져간 되읽기 — 시리얼 없이 바로 돌아온다
    SW_EXPECT_TRUE( ring.hasLiveSolo() == false );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), ring.getLiveCount() );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 1024, 0, probe ) ) );
    ring.free( probe );

    ring.initialize( 1024, 256, 0 );
    SW_EXPECT_TRUE( ring.allocate( 1025, 0, probe ) == sw::RHIStagingAllocateResult::TooLarge ); // 단독 구간을 받지 않는 장부
}
