#include "pch.h"

#include "Core/Concurrency/atomic.h"

#include "Editor/Common/Backend/DrawSnapshotHandoff.h"

#include "TestFramework/TestFramework.h"

#include <thread>

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 스레드마다 따로 쓰는 작은 의사 난수(xorshift32)입니다. 두 스레드의 호출 순서를 섞는 데만 씁니다. */
    struct HandoffTestRandom
    {
        /** @brief 동시 테스트에서 UI 스레드가 게시하는 프레임 수입니다. */
        static constexpr uint32 kConcurrentFrameCount = 4000;

        uint32 _state;

        explicit HandoffTestRandom( uint32 seed )
            : _state{ seed }
        {
        }

        uint32 next()
        {
            _state ^= _state << 13;
            _state ^= _state >> 17;
            _state ^= _state << 5;
            return _state;
        }

        /** @brief 0 이상 `bound` 미만의 값입니다. */
        uint32 nextBelow( uint32 bound ) { return next() % bound; }

        /** @brief 0 번 이상 `maxYieldCount` 번 미만 양보해 상대 스레드가 끼어들 틈을 만듭니다. */
        void yieldRandomly( uint32 maxYieldCount )
        {
            const uint32 yieldCount = nextBelow( maxYieldCount );
            for ( uint32 index = 0; index < yieldCount; ++index )
            {
                std::this_thread::yield();
            }
        }
    };

    /** @brief 슬롯마다 지금 쓰는 쪽 · 읽는 쪽이 있는지 표시하고, 둘이 겹친 횟수를 셉니다. */
    struct HandoffSlotUsage
    {
        atomic<uint32> _arrWriterCount[constant::kMaxFrameCountInFlight];
        atomic<uint32> _arrReaderCount[constant::kMaxFrameCountInFlight];
        atomic<uint32> _overlapCount;

        HandoffSlotUsage()
            : _arrWriterCount{}
            , _arrReaderCount{}
            , _overlapCount{ 0 }
        {
        }

        void beginWrite( uint32 slot )
        {
            _arrWriterCount[slot].store( 1 );
            if ( _arrReaderCount[slot].load() != 0 )
                _overlapCount.fetch_add( 1 );
        }

        void endWrite( uint32 slot )
        {
            if ( _arrReaderCount[slot].load() != 0 )
                _overlapCount.fetch_add( 1 );
            _arrWriterCount[slot].store( 0 );
        }

        void beginRead( uint32 slot )
        {
            _arrReaderCount[slot].store( 1 );
            if ( _arrWriterCount[slot].load() != 0 )
                _overlapCount.fetch_add( 1 );
        }

        void endRead( uint32 slot )
        {
            if ( _arrWriterCount[slot].load() != 0 )
                _overlapCount.fetch_add( 1 );
            _arrReaderCount[slot].store( 0 );
        }
    };
} // namespace

/**
 * @brief [DrawSnapshotHandoffTest] 처음에는 렌더 대기가 없고, 게시하면 렌더 대기가 되며, 렌더가 끝나면 다시 빈다
 */
SW_TEST_CASE( DrawSnapshotHandoffTest, PublishMarksInFlightUntilRenderEnds )
{
    DrawSnapshotHandoff handoff;
    SW_EXPECT_TRUE( handoff.isIdle() );
    SW_EXPECT_EQUAL( 0u, handoff.getPublishedSlot() );

    // 첫 게시는 게시 슬롯(0)의 다음 슬롯에 쓴다.
    const uint32 writeSlot = handoff.waitForWriteSlot();
    SW_EXPECT_EQUAL( 1u % constant::kMaxFrameCountInFlight, writeSlot );
    handoff.publish( writeSlot );
    SW_EXPECT_EQUAL( writeSlot, handoff.getPublishedSlot() );
    SW_EXPECT_FALSE( handoff.isIdle() );

    // 렌더 스레드는 게시 슬롯을 읽는다. 같은 스냅샷을 여러 번 다시 그려도 같은 슬롯이다.
    SW_EXPECT_EQUAL( writeSlot, handoff.beginRender() );
    SW_EXPECT_FALSE( handoff.isIdle() );
    handoff.endRender();
    SW_EXPECT_TRUE( handoff.isIdle() );
    SW_EXPECT_EQUAL( writeSlot, handoff.beginRender() );
    handoff.endRender();

    // 다음 프레임은 그다음 슬롯에 쓴다. 렌더 스레드가 읽는 슬롯과 다르다.
    const uint32 nextWriteSlot = handoff.waitForWriteSlot();
    SW_EXPECT_NOT_EQUAL( writeSlot, nextWriteSlot );
    SW_EXPECT_EQUAL( ( writeSlot + 1u ) % constant::kMaxFrameCountInFlight, nextWriteSlot );
}

/**
 * @brief [DrawSnapshotHandoffTest] 렌더 스레드가 그리는 동안 UI 스레드가 받는 쓰기 슬롯은 그 슬롯이 아니다
 */
SW_TEST_CASE( DrawSnapshotHandoffTest, WriteSlotNeverEqualsTheSlotBeingRendered )
{
    DrawSnapshotHandoff handoff;
    for ( uint32 frame = 0; frame < constant::kMaxFrameCountInFlight * 3u; ++frame )
    {
        handoff.waitForIdle();
        const uint32 writeSlot = handoff.waitForWriteSlot();
        handoff.publish( writeSlot );

        const uint32 renderSlot = handoff.beginRender();
        SW_EXPECT_EQUAL( writeSlot, renderSlot );
        // 렌더 중에 UI 가 다음 프레임 슬롯을 물어도 그리는 슬롯은 나오지 않는다.
        SW_EXPECT_NOT_EQUAL( renderSlot, handoff.waitForWriteSlot() );
        handoff.endRender();
    }
}

/**
 * @brief [DrawSnapshotHandoffTest] 렌더 스레드가 받지 못한 프레임의 렌더 대기는 포기로만 풀린다
 * @details 인스턴스를 없앨 때 렌더 워커는 이미 비어 있어 `endRender` 를 부를 쪽이 없다. 포기하지 않으면 종료 단계의 `waitForIdle` 이 반환하지 않는다.
 */
SW_TEST_CASE( DrawSnapshotHandoffTest, AbandonReleasesAFrameTheRenderThreadNeverTook )
{
    DrawSnapshotHandoff handoff;
    handoff.publish( handoff.waitForWriteSlot() );
    SW_EXPECT_FALSE( handoff.isIdle() );

    const uint32 publishedSlot = handoff.getPublishedSlot();
    handoff.abandon();
    SW_EXPECT_TRUE( handoff.isIdle() );
    // 포기는 게시 슬롯을 바꾸지 않는다. 다음 인스턴스가 없더라도 남은 값이 엇갈리지 않는다.
    SW_EXPECT_EQUAL( publishedSlot, handoff.getPublishedSlot() );
    handoff.waitForIdle();
}

/**
 * @brief [DrawSnapshotHandoffTest] UI 스레드와 렌더 스레드가 임의 순서로 게시 · 렌더를 부르고 끝에 포기해도 같은 슬롯을 함께 쓰지 않는다
 * @details 렌더 스레드는 매번 그리지 않고 임의로 프레임을 건너뛴다(게임 모듈 리로드로 프레임이 제출되지 않는 경우). UI 스레드는 그동안
 *          `waitForIdle` 에서 기다린다. 두 스레드가 끝나면(렌더 워커를 비운 뒤) 인스턴스를 없애는 쪽처럼 포기해 남은 렌더 대기를 푼다.
 *          겹침은 슬롯마다 쓰는 쪽 · 읽는 쪽 표시를 서로 확인해 센다.
 */
SW_TEST_CASE( DrawSnapshotHandoffTest, ConcurrentPublishRenderAndAbandonNeverShareASlot )
{
    DrawSnapshotHandoff handoff;
    HandoffSlotUsage    usage;
    atomic<uint32>      bUIDone{ 0 };
    atomic<uint32>      renderCount{ 0 };

    std::thread uiThread( [&handoff, &usage, &bUIDone]()
    {
        HandoffTestRandom random{ 0x9E3779B9u };
        for ( uint32 frame = 0; frame < HandoffTestRandom::kConcurrentFrameCount; ++frame )
        {
            handoff.waitForIdle();
            const uint32 writeSlot = handoff.waitForWriteSlot();

            usage.beginWrite( writeSlot );
            random.yieldRandomly( 8u );
            usage.endWrite( writeSlot );

            handoff.publish( writeSlot );
            random.yieldRandomly( 4u );
        }
        bUIDone.store( 1 );
    } );

    std::thread renderThread( [&handoff, &usage, &bUIDone, &renderCount]()
    {
        HandoffTestRandom random{ 0x85EBCA6Bu };
        while ( bUIDone.load() == 0 )
        {
            // 세 번에 한 번은 이 프레임을 그리지 않는다.
            if ( random.nextBelow( 3u ) == 0u )
            {
                random.yieldRandomly( 8u );
                continue;
            }

            const uint32 slot = handoff.beginRender();
            usage.beginRead( slot );
            random.yieldRandomly( 8u );
            usage.endRead( slot );
            handoff.endRender();
            renderCount.fetch_add( 1 );
        }
    } );

    uiThread.join();
    renderThread.join();

    // 렌더 워커를 비웠다. 마지막 게시가 렌더 대기로 남았을 수 있으므로 인스턴스를 없애는 쪽처럼 포기한다.
    handoff.abandon();
    SW_EXPECT_TRUE( handoff.isIdle() );
    handoff.waitForIdle();

    SW_EXPECT_EQUAL( 0u, usage._overlapCount.load() );
    SW_EXPECT_TRUE( renderCount.load() > 0u );
}
