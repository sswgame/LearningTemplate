#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIStagingAllocator.h"
#include "Engine/Graphics/RHI/Support/RHIStagingBudget.h"
#include "Engine/Graphics/RHI/Support/RHIStagingRingUtil.h"
#include "Engine/Graphics/RHI/Support/RHITransferSerials.h"

#include "TestFramework/TestFramework.h"

// 업로드 · 되읽기 링(RHIStagingAllocator)의 장부 규칙을 GPU 없이 본다. 그 링을 쓰는 GL · Vulkan 전송은 TestRHIStagingRingGPU.cpp 가 실제 디바이스로 본다.

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
 * @brief [RHIStagingRingTest] 장치를 잃은 뒤에는 링이 차 있어도 Busy 가 아니라 Rejected 다(계약 2)
 * @details 잃은 장치의 펜스는 끝나지 않아 워터마크가 멈추고 링이 다시 비지 않는다. 그때 Full 을 Busy 로 옮기면 `submitWhileBusy` 와 다음 프레임의 재제출이
 *          끝없이 되풀이한다. 할당 결과를 제출 결과로 옮기는 곳은 `RHITransferSerials::classifyStagingAllocation` 하나이고, 백엔드 제출 함수는 진입에서 `isLost` 를 본다.
 */
SW_TEST_CASE( RHIStagingRingTest, LostDeviceRejectsInsteadOfBusyWhenTheRingIsFull )
{
    sw::RHIStagingAllocator ring;
    sw::RHITransferSerials  serials;
    ring.initialize( 256, 16, 0 );

    sw::RHIStagingSpan held{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, serials.getWatermark(), held ) ) );
    ring.markSubmitted( held, serials.issue() ); // 제출했지만 GPU 가 끝내지 않았다(워터마크 0)

    sw::RHIStagingSpan                 probe{};
    const sw::RHIStagingAllocateResult fullBeforeLoss = ring.allocate( 16, serials.getWatermark(), probe );
    SW_ASSERT_TRUE( fullBeforeLoss == sw::RHIStagingAllocateResult::Full );
    SW_EXPECT_FALSE( serials.isLost() );
    SW_EXPECT_TRUE( serials.classifyStagingAllocation( fullBeforeLoss ) == sw::RHISubmitResult::Busy ); // GPU 가 진행하면 빈다
    SW_EXPECT_TRUE( serials.classifyStagingAllocation( sw::RHIStagingAllocateResult::Allocated ) == sw::RHISubmitResult::Accepted );
    SW_EXPECT_TRUE( serials.classifyStagingAllocation( sw::RHIStagingAllocateResult::TooLarge ) == sw::RHISubmitResult::Rejected );

    serials.markLost();
    const sw::RHIStagingAllocateResult fullAfterLoss = ring.allocate( 16, serials.getWatermark(), probe );
    SW_ASSERT_TRUE( fullAfterLoss == sw::RHIStagingAllocateResult::Full ); // 워터마크가 멈춰 링은 그대로 차 있다
    SW_EXPECT_TRUE( serials.isLost() );
    SW_EXPECT_TRUE( serials.classifyStagingAllocation( fullAfterLoss ) == sw::RHISubmitResult::Rejected );
    SW_EXPECT_TRUE( serials.classifyStagingAllocation( sw::RHIStagingAllocateResult::Allocated ) == sw::RHISubmitResult::Rejected ); // 잡은 구간은 호출자가 놓는다
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
 * @brief [RHIStagingRingTest] 손실 표시는 처음 표시한 호출만 true 를 돌려준다 — 손실 상태의 저장소가 이 표시 하나라서 "처음 본 쪽만 로그를 남긴다" 가 여기서 선다
 * @details Vulkan 은 자기 손실 플래그를 따로 두었다가 전송 시리얼에 표시하지 않아 제출 진입의 `isLost` 가 손실을 놓쳤다. 지금은 `VulkanRHIDevice::isDeviceLost` 가 이 표시를 읽는다.
 */
SW_TEST_CASE( RHIStagingRingTest, MarkLostReportsOnlyTheFirstMark )
{
    sw::RHITransferSerials serials;
    SW_EXPECT_FALSE( serials.isLost() );
    SW_EXPECT_TRUE( serials.markLost() );
    SW_EXPECT_TRUE( serials.isLost() );
    SW_EXPECT_FALSE( serials.markLost() ); // 이미 잃은 상태다
    SW_EXPECT_TRUE( serials.isLost() );
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

/**
 * @brief [RHIStagingRingTest] 되읽기 링(Vulkan): 구간은 결과를 가져갈 때까지 남고, 끝나기 전에 버린 구간은 그 복사의 시리얼이 워터마크를 지난 뒤에 돌아온다
 * @details 되읽기 구간에 제출 때 시리얼을 붙이면 복사가 끝나는 순간 돌아와 가져가기 전에 다음 되읽기가 덮는다. 끝나기 전에 버린 구간을 바로 놓으면 GPU 가 아직 쓰는 자리를
 *          다음 되읽기가 받는다. 그래서 `VulkanRHIResourceFactory` 는 가져갈 때 `free`, 끝나기 전에 버릴 때 `markSubmitted` 를 부른다. 장치 종료는 보관표를 비운다(`clear`).
 */
SW_TEST_CASE( RHIStagingRingTest, ReadbackSpansStayUntilTakenAndDiscardedOnesWaitForTheirCopy )
{
    sw::RHIStagingAllocator                         ring;
    sw::RHITransferSerials                          serials;
    sw::RHIPendingReadbackTable<sw::RHIStagingSpan> table;
    ring.initialize( 512, 256, 0 );

    sw::RHIStagingSpan taken{};
    sw::RHIStagingSpan discarded{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, serials.getWatermark(), taken ) ) );
    const uint64 takenSerial = serials.issue();
    table.add( takenSerial, taken );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, serials.getWatermark(), discarded ) ) );
    const uint64 discardedSerial = serials.issue();
    table.add( discardedSerial, discarded );

    // 첫 복사가 끝났지만 아직 가져가지 않았다 — 구간은 돌아오지 않는다.
    serials.advanceWatermark( takenSerial );
    sw::RHIStagingSpan probe{};
    SW_EXPECT_TRUE( ring.allocate( 16, serials.getWatermark(), probe ) == sw::RHIStagingAllocateResult::Full );

    // 둘째는 끝나기 전에 버린다(시리얼을 붙인다), 첫째는 가져간다(놓는다).
    sw::RHIStagingSpan out{};
    SW_ASSERT_TRUE( table.take( discardedSerial, out ) );
    SW_EXPECT_FALSE( serials.isComplete( discardedSerial ) );
    ring.markSubmitted( out, discardedSerial );
    SW_ASSERT_TRUE( table.take( takenSerial, out ) );
    ring.free( out );
    ring.freeCompleted( serials.getWatermark() );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() ); // 버린 구간은 복사가 아직 쓰고 있다
    SW_EXPECT_TRUE( ring.allocate( 512, serials.getWatermark(), probe ) == sw::RHIStagingAllocateResult::Full );

    serials.advanceWatermark( discardedSerial );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 512, serials.getWatermark(), probe ) ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), probe._offsetBytes );

    table.add( serials.issue(), probe );
    table.clear();
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), table.getCount() );
}

/**
 * @brief [RHIStagingRingTest] 가짜 장치로 본 Vulkan 업로드의 링 재사용: 펜스가 앞에서부터 연속으로 지난 만큼만 구간이 돌아오고, 뒤의 펜스가 먼저 신호돼도 앞 구간을 덮지 않는다
 * @details `VulkanRHIDevice` 의 순서를 흉내 낸다 — 링 할당, 제출과 같은 락 안의 시리얼(`RHITransferFenceQueue::enqueue`), 구간에 시리얼 붙이기(`markSubmitted`),
 *          펜스 확인(`retireDone`)과 작업 재사용. 펜스는 작업마다 따로라 신호 순서가 제출 순서와 다를 수 있다. 링이 찬 동안의 결과는 `Busy` 다.
 */
SW_TEST_CASE( RHIStagingRingTest, FakeDeviceReusesRingSpansOnlyAfterTheFencePrefixPasses )
{
    struct FakeJob
    {
        uint32 _fenceIndex;
    };

    sw::RHITransferSerials             serials;
    sw::RHITransferFenceQueue<FakeJob> fenceQueue{ serials };
    sw::RHIStagingAllocator            ring;
    ring.initialize( 768, 256, 0 );

    constexpr uint32   kJobCount = 3;
    bool               arrSignaled[kJobCount]{ false, false, false };
    sw::RHIStagingSpan arrSpan[kJobCount]{};
    uint64             arrSerial[kJobCount]{};
    uint32             recycledCount{ 0 };
    const auto         isSignaled = [&arrSignaled]( const FakeJob& job )
    { return arrSignaled[job._fenceIndex]; };
    const auto recycle = [&recycledCount]( FakeJob& )
    { ++recycledCount; };

    for ( uint32 jobIndex = 0; jobIndex < kJobCount; ++jobIndex )
    {
        SW_ASSERT_TRUE( isAllocated( ring.allocate( 200, serials.getWatermark(), arrSpan[jobIndex] ) ) );
        arrSerial[jobIndex] = fenceQueue.enqueue( FakeJob{ jobIndex } );
        ring.markSubmitted( arrSpan[jobIndex], arrSerial[jobIndex] );
    }

    // 뒤의 두 펜스가 먼저 신호됐다. 걷어도 맨 앞이 안 끝나 워터마크가 그대로이고 링은 차 있다(Busy).
    arrSignaled[1] = true;
    arrSignaled[2] = true;
    SW_EXPECT_FALSE( fenceQueue.retireDone( isSignaled, recycle ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), recycledCount );
    SW_EXPECT_FALSE( serials.isComplete( arrSerial[2] ) );
    sw::RHIStagingSpan                 next{};
    const sw::RHIStagingAllocateResult fullResult = ring.allocate( 200, serials.getWatermark(), next );
    SW_EXPECT_TRUE( serials.classifyStagingAllocation( fullResult ) == sw::RHISubmitResult::Busy );

    // 맨 앞이 끝나면 셋이 함께 걷혀 작업이 재사용 목록으로 돌아가고, 링은 처음부터 다시 받는다.
    arrSignaled[0] = true;
    SW_EXPECT_TRUE( fenceQueue.retireDone( isSignaled, recycle ) );
    SW_EXPECT_EQUAL( kJobCount, recycledCount );
    SW_EXPECT_EQUAL( arrSerial[2], serials.getWatermark() );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 600, serials.getWatermark(), next ) ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), next._offsetBytes );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() );
}

/**
 * @brief [RHIStagingRingTest] 할당 결과의 마무리는 한 곳이다: 받으면 구간을 남기고, Busy 는 조용히, 거절은 구간을 놓고 이유를 한 줄 남긴다
 * @details DX12 · Vulkan · GL 의 `allocateStagingSpan` 이 같이 부르는 `RHIStagingRingUtil::settleAllocation` 과 단독 버퍼 생성 실패의 `rejectSoloBufferFailure` 를 GPU 없이 본다.
 */
SW_TEST_CASE( RHIStagingRingTest, SettleAllocationKeepsAcceptedAndBusyQuietAndFreesRejectedSpans )
{
    sw::RHIStagingAllocator ring;
    sw::RHITransferSerials  serials;
    ring.initialize( 256, 16, 1024 );
    test::ScopedLogCollector logs;

    sw::RHIStagingSpan held{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 128, serials.getWatermark(), held ) ) );
    SW_EXPECT_TRUE( sw::RHIStagingRingUtil::settleAllocation( ring, serials, sw::RHIStagingAllocateResult::Allocated, 128, "Test", "settleProbe", held ) == sw::RHISubmitResult::Accepted );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() ); // 받았으면 구간은 호출자 것이다

    sw::RHIStagingSpan none{};
    SW_EXPECT_TRUE( sw::RHIStagingRingUtil::settleAllocation( ring, serials, sw::RHIStagingAllocateResult::Full, 128, "Test", "settleProbe", none ) == sw::RHISubmitResult::Busy );
    SW_EXPECT_EQUAL( 0u, logs.count() ); // Busy 는 오류가 아니다

    {
        SW_TEST_DEFENSIVE_SCOPE( "a request over the single transfer limit is rejected with one log line" );
        SW_EXPECT_TRUE( sw::RHIStagingRingUtil::settleAllocation( ring, serials, sw::RHIStagingAllocateResult::TooLarge, 4096, "Test", "settleProbe", none ) == sw::RHISubmitResult::Rejected );
    }
    SW_EXPECT_EQUAL( 1u, logs.count() );

    sw::RHIStagingSpan solo{};
    sw::RHIStagingSpan second{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 64, serials.getWatermark(), second ) ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), ring.getLiveCount() );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a lost device rejects and frees the span it had allocated" );
        serials.markLost();
        SW_EXPECT_TRUE( sw::RHIStagingRingUtil::settleAllocation( ring, serials, sw::RHIStagingAllocateResult::Allocated, 64, "Test", "settleProbe", second ) == sw::RHISubmitResult::Rejected );
    }
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() ); // 거절된 구간은 놓였다
    SW_EXPECT_EQUAL( 2u, logs.count() );

    ring.free( held );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 512, serials.getWatermark(), solo ) ) ); // 링(256)보다 크고 단독 상한(1024) 이하라 단독 구간이다
    SW_ASSERT_TRUE( solo._bSolo != SW_FALSE );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a single transfer buffer that could not be created rejects and frees the span" );
        SW_EXPECT_TRUE( sw::RHIStagingRingUtil::rejectSoloBufferFailure( ring, solo, "Test", "settleProbe" ) == sw::RHISubmitResult::Rejected );
    }
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), ring.getLiveCount() );
    SW_EXPECT_EQUAL( 3u, logs.count() );
}

/**
 * @brief [RHIStagingRingTest] 스테이징 할당이 받지 못한 결과는 하나의 변환으로 제출 결과가 된다: Busy 는 busy, 나머지는 rejected
 * @details 두 백엔드가 각자 갖던 `makeUnacceptedSubmit` 과 GL 의 인라인 삼항식을 `RHITransferSubmit::fromUnaccepted` 하나로 합쳤다.
 */
SW_TEST_CASE( RHIStagingRingTest, UnacceptedSubmitMapsBusyAndRejectedWithoutATicket )
{
    const sw::RHITransferSubmit busy     = sw::RHITransferSubmit::fromUnaccepted( sw::RHISubmitResult::Busy );
    const sw::RHITransferSubmit rejected = sw::RHITransferSubmit::fromUnaccepted( sw::RHISubmitResult::Rejected );
    SW_EXPECT_TRUE( busy._result == sw::RHISubmitResult::Busy );
    SW_EXPECT_TRUE( rejected._result == sw::RHISubmitResult::Rejected );
    SW_EXPECT_FALSE( busy.isAccepted() );
    SW_EXPECT_FALSE( rejected.isAccepted() );
    SW_EXPECT_FALSE( busy._ticket.isValid() );
    SW_EXPECT_FALSE( rejected._ticket.isValid() );
}

/**
 * @brief [RHIStagingRingTest] DX11 업로드 예산(`RHIStagingBudget`): 복사 중인 합이 예산을 넘지 않고, 돌려준 바이트만큼 다시 받는다
 * @details 예산은 오프셋이 없는 총량이라 조각 남음으로 거절하지 않는다 — 합이 들어가면 받는다. 못 받은 요청이 `outReservedBytes` 를 0 으로 둔다.
 */
SW_TEST_CASE( RHIStagingRingTest, UploadBudgetKeepsTheInFlightSumWithinTheBudget )
{
    sw::RHIStagingBudget budget;
    budget.initialize( 1000, 0 );

    uint64 a{ 99 };
    uint64 b{ 99 };
    uint64 c{ 99 };
    SW_ASSERT_TRUE( budget.tryReserve( 600, 0, a ) );
    SW_ASSERT_TRUE( budget.tryReserve( 400, 0, b ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 600 ), a );
    SW_EXPECT_EQUAL( static_cast<uint64>( 1000 ), budget.getInFlightBytes() );
    SW_EXPECT_FALSE( budget.tryReserve( 1, 0, c ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), c ); // 못 받으면 0 이다

    budget.release( a );
    SW_EXPECT_EQUAL( static_cast<uint64>( 400 ), budget.getInFlightBytes() );
    SW_ASSERT_TRUE( budget.tryReserve( 500, 0, c ) ); // 앞에서 돌려받은 600 안에 들어간다
    SW_EXPECT_FALSE( budget.tryReserve( 101, 0, a ) );
    SW_EXPECT_TRUE( budget.tryReserve( 100, 0, a ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 1000 ), budget.getInFlightBytes() );

    budget.release( a );
    budget.release( b );
    budget.release( c );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), budget.getInFlightBytes() );
    SW_EXPECT_TRUE( budget.tryReserve( 0, 0, a ) ); // 0 바이트는 아무것도 잡지 않고 받는다
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), budget.getInFlightBytes() );
}

/**
 * @brief [RHIStagingRingTest] 예산보다 큰 한 장은 아무것도 복사 중이지 않을 때 예산 전부를 잡고, 못 받은 동안 작은 요청을 막아 굶지 않는다
 * @details 작은 업로드가 끊이지 않으면 복사 중인 합이 0 이 되는 순간이 없어 큰 한 장이 영원히 `Busy` 다 — 장부(`RHIStagingAllocator`)의 단독 구간 예약과 같은 규칙.
 */
SW_TEST_CASE( RHIStagingRingTest, UploadBudgetWholeRequestWaitsForEmptyAndBlocksSmallOnesMeanwhile )
{
    constexpr int64      kLongReservationMicroseconds = 1000ll * 1000; // 테스트가 쓰는 시각 안에서 끝나지 않는 예약
    sw::RHIStagingBudget budget;
    budget.initialize( 1000, kLongReservationMicroseconds );

    uint64 small{ 0 };
    uint64 whole{ 0 };
    uint64 probe{ 0 };
    SW_ASSERT_TRUE( budget.tryReserve( 100, 10, small ) );
    SW_EXPECT_FALSE( budget.tryReserve( 5000, 10, whole ) ); // 복사 중인 것이 있다 — 기다리면 받는다
    SW_EXPECT_FALSE( budget.tryReserve( 10, 20, probe ) );   // 큰 요청이 기다린다 — 자리가 있어도 새 작은 요청은 받지 않는다
    budget.release( small );
    SW_EXPECT_FALSE( budget.tryReserve( 10, 30, probe ) ); // 비었어도 큰 요청이 다시 오기 전에는 예약이 산다

    SW_ASSERT_TRUE( budget.tryReserve( 5000, 40, whole ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 1000 ), whole ); // 예산 전부를 잡는다
    SW_EXPECT_EQUAL( static_cast<uint64>( 1000 ), budget.getInFlightBytes() );
    SW_EXPECT_FALSE( budget.tryReserve( 10, 50, probe ) ); // 혼자 들어갔다 — 다른 업로드는 자리가 없다
    budget.release( whole );
    SW_EXPECT_TRUE( budget.tryReserve( 10, 60, probe ) ); // 큰 요청을 받으면서 예약이 끝났다
    budget.release( probe );

    // 예산과 같은 크기도 예산 전부를 잡는 요청이다.
    SW_ASSERT_TRUE( budget.tryReserve( 100, 70, small ) );
    SW_EXPECT_FALSE( budget.tryReserve( 1000, 70, whole ) );
    SW_EXPECT_FALSE( budget.tryReserve( 10, 80, probe ) );
}

/**
 * @brief [RHIStagingRingTest] 다시 오지 않는 큰 요청의 예약은 끝나 작은 요청을 다시 받고, 예약 시간 0 은 예약하지 않는다
 */
SW_TEST_CASE( RHIStagingRingTest, UploadBudgetAbandonedWholeRequestStopsBlockingSmallOnes )
{
    sw::RHIStagingBudget budget;
    budget.initialize( 1000, 100 );

    uint64 held{ 0 };
    uint64 whole{ 0 };
    uint64 probe{ 0 };
    SW_ASSERT_TRUE( budget.tryReserve( 100, 1000, held ) );
    SW_EXPECT_FALSE( budget.tryReserve( 2000, 1000, whole ) ); // 예약은 1100 까지
    SW_EXPECT_FALSE( budget.tryReserve( 10, 1099, probe ) );
    SW_EXPECT_TRUE( budget.tryReserve( 10, 1100, probe ) ); // 큰 요청이 다시 오지 않았다 — 막지 않는다
    SW_EXPECT_EQUAL( static_cast<uint64>( 110 ), budget.getInFlightBytes() );
    budget.release( held );
    budget.release( probe );

    budget.initialize( 1000, 0 );
    SW_ASSERT_TRUE( budget.tryReserve( 100, 1000, held ) );
    SW_EXPECT_FALSE( budget.tryReserve( 2000, 1000, whole ) );
    SW_EXPECT_TRUE( budget.tryReserve( 10, 1000, probe ) ); // 예약 시간 0 — 큰 요청이 작은 요청을 막지 않는다
}
