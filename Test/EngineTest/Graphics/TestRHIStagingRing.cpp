#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIStagingRing.h"
#include "Engine/Graphics/RHI/Support/RHITransferSerials.h"

#include "TestFramework/TestFramework.h"

// 업로드 스테이징 링(RHIStagingRing)의 장부 규칙을 GPU 없이 본다. 그 링을 쓰는 GL 업로드는 TestRHIStagingRingGPU.cpp 가 실제 디바이스로 본다.

namespace
{
    /** @brief 할당 결과가 `Allocated` 인지 묻습니다(열거값을 테스트 메시지 없이 비교하는 줄을 줄인다). */
    bool isAllocated( sw::RHIStagingAllocateResult result )
    {
        return result == sw::RHIStagingAllocateResult::Allocated;
    }
} // namespace

/**
 * @brief [RHIStagingRingTest] 구간은 정렬된 오프셋에 차례로 잡히고, 끝에 자리가 없으면 맨 앞이 돌아온 만큼만 처음으로 감는다
 * @details 링보다 큰 요청과 0 바이트는 기다려도 못 받으므로 `TooLarge`(제출의 Rejected), 지금 자리가 없을 뿐이면 `Full`(Busy)이다.
 */
SW_TEST_CASE( RHIStagingRingTest, AllocatesAlignedSpansAndWrapsAfterTheFrontCompletes )
{
    sw::RHIStagingRing ring;
    ring.initialize( 1024, 256 );

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
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), rejected._id );                                 // 못 잡은 구간은 빈 값이다
    SW_EXPECT_TRUE( ring.allocate( 1025, 0, rejected ) == sw::RHIStagingAllocateResult::TooLarge );
    SW_EXPECT_TRUE( ring.allocate( 0, 0, rejected ) == sw::RHIStagingAllocateResult::TooLarge );

    ring.markSubmitted( a, 1 );
    ring.markSubmitted( b, 2 );
    ring.markSubmitted( c, 3 );

    // 워터마크 1: a 만 돌아온다. 앞의 빈 곳은 [0, 256) 이라 300 은 못 받고 200 은 처음으로 감아 받는다.
    sw::RHIStagingSpan wrapped{};
    SW_EXPECT_TRUE( ring.allocate( 300, 1, rejected ) == sw::RHIStagingAllocateResult::Full );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 200, 1, wrapped ) ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), wrapped._offsetBytes );
    // 감긴 뒤의 빈 곳은 [256, 256) — 뒤 구간의 끝에서 맨 앞 구간 사이다.
    SW_EXPECT_TRUE( ring.allocate( 16, 1, rejected ) == sw::RHIStagingAllocateResult::Full );

    // 워터마크 3: b · c 가 돌아오고, 아직 제출하지 않은 감긴 구간(시리얼 0)이 맨 앞에 남는다.
    sw::RHIStagingSpan afterWrap{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 700, 3, afterWrap ) ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 256 ), afterWrap._offsetBytes );
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), ring.getLiveCount() );
}

/**
 * @brief [RHIStagingRingTest] 회수는 할당 순서로만 한다 — 뒤 구간이 먼저 끝나도, 제출하지 않은 구간이 앞에 있어도 거기서 멈춘다
 * @details 시리얼은 컨텍스트 · 큐 락 안에서 매기므로 먼저 잡은 스레드가 늦게 제출할 수 있다. 앞을 건너뛰면 GPU 가 아직 읽는 구간을 덮는다.
 *          제출하지 못하고 놓은 구간(`free`)은 맨 앞에 오면 시리얼 없이 돌아온다.
 */
SW_TEST_CASE( RHIStagingRingTest, FreesInAllocationOrderOnly )
{
    sw::RHIStagingRing ring;
    ring.initialize( 512, 16 );

    sw::RHIStagingSpan first{};
    sw::RHIStagingSpan second{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, first ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 256, 0, second ) ) );
    ring.markSubmitted( second, 1 ); // 뒤에 잡은 쪽이 먼저 제출했다
    ring.markSubmitted( first, 2 );

    sw::RHIStagingSpan probe{};
    SW_EXPECT_TRUE( ring.allocate( 16, 1, probe ) == sw::RHIStagingAllocateResult::Full ); // second 는 끝났지만 맨 앞 first 가 아직이다
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), ring.getLiveCount() );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 16, 2, probe ) ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), probe._offsetBytes );
    SW_EXPECT_EQUAL( static_cast<uint32>( 1 ), ring.getLiveCount() );

    ring.free( probe );
    ring.freeCompleted( 0 );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), ring.getLiveCount() );

    sw::RHIStagingSpan unsubmitted{};
    sw::RHIStagingSpan submitted{};
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 128, 2, unsubmitted ) ) );
    SW_ASSERT_TRUE( isAllocated( ring.allocate( 128, 2, submitted ) ) );
    ring.markSubmitted( submitted, 5 );
    ring.freeCompleted( 10 );
    SW_EXPECT_EQUAL( static_cast<uint32>( 2 ), ring.getLiveCount() ); // 맨 앞이 아직 제출 전(복사 중)이다
    ring.free( unsubmitted );
    ring.freeCompleted( 10 );
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
    sw::RHIStagingRing     ring;
    sw::RHITransferSerials serials;
    ring.initialize( 256, 16 );

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
