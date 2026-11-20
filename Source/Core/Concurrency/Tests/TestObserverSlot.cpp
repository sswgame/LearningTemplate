/**
 * @file TestObserverSlot.cpp
 * @brief ObserverSlot — 빈 칸에만 걸기, 이중 등록 단언(덮어쓰지 않음), tryBind 의 말없는 빈 토큰, 토큰 이동 · 해제, 늦게 풀린 옛 토큰이 새 값을 지우지 않음.
 *        할당 계수기(`ScopedAllocationCount`)가 할당 관찰자를 건드리지 않는지도 본다.
 */
#include "pch.h"

#include "Core/Concurrency/ObserverSlot.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/ScopedAllocationCount.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

namespace
{
    struct ObserverSlotTestInternal
    {
        int32 _value{ 0 };
    };

    /** @brief 할당 알림을 세는 관찰자입니다(다른 스레드의 할당도 알림을 받는다). */
    struct ObserverSlotAllocationCounterInternal
    {
        static void                      onAllocate( const void*, size_t, sw::MemoryTag ) { s_allocateCount.fetch_add( 1 ); }
        static void                      onFree( const void*, sw::MemoryTag ) {}
        static inline sw::atomic<uint32> s_allocateCount{ 0 };
    };
} // namespace

/**
 * @brief [ObserverSlotTest] 빈 칸에 걸면 읽히고, 토큰이 사라지면 칸이 빈다
 */
SW_TEST_CASE( ObserverSlotTest, BindThenReleaseOnDestruction )
{
    sw::ObserverSlot<ObserverSlotTestInternal> slot;
    ObserverSlotTestInternal                   observer;
    SW_EXPECT_NULL( slot.get() );
    {
        const sw::ObserverSlot<ObserverSlotTestInternal>::Registration registration = slot.bind( &observer );
        SW_EXPECT_TRUE( registration.isActive() );
        SW_EXPECT_TRUE( slot.get() == &observer );
    }
    SW_EXPECT_NULL( slot.get() );
}

/**
 * @brief [ObserverSlotTest] 이미 걸린 칸에 bind 하면 단언으로 알리고 앞 값을 덮어쓰지 않는다 · tryBind 는 말없이 빈 토큰이다
 */
SW_TEST_CASE( ObserverSlotTest, SecondBindDoesNotOverwrite )
{
    sw::ObserverSlot<ObserverSlotTestInternal> slot;
    ObserverSlotTestInternal                   first;
    ObserverSlotTestInternal                   second;
    const auto                                 firstRegistration = slot.bind( &first );
    SW_REQUIRE_TRUE( firstRegistration.isActive() );

    {
        sw::test::ScopedAssertCapture capture;
        SW_EXPECT_LOG( SW_ASSERT_LOG_ID ); // 이중 등록 단언은 이 테스트가 보려는 대상이다(배포 구성에서도 보고는 남는다)
        const auto secondRegistration = slot.bind( &second );
        SW_EXPECT_FALSE( secondRegistration.isActive() );
        if ( sw::test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 1u, capture.getCount() );
    }
    SW_EXPECT_TRUE( slot.get() == &first );

    const auto quietRegistration = slot.tryBind( &second );
    SW_EXPECT_FALSE( quietRegistration.isActive() );
    SW_EXPECT_TRUE( slot.get() == &first );
}

/**
 * @brief [ObserverSlotTest] 토큰을 옮기면 해제 책임도 옮겨 가고, 이미 풀린 칸을 다른 쪽이 건 뒤 옛 토큰을 풀어도 새 값은 남는다
 */
SW_TEST_CASE( ObserverSlotTest, MovedAndStaleRegistrationsReleaseOnlyTheirOwnValue )
{
    sw::ObserverSlot<ObserverSlotTestInternal> slot;
    ObserverSlotTestInternal                   first;
    ObserverSlotTestInternal                   second;

    auto registration = slot.bind( &first );
    auto moved        = std::move( registration );
    SW_EXPECT_FALSE( registration.isActive() ); // NOLINT(bugprone-use-after-move) 옮긴 뒤 빈 토큰인지가 계약이다
    SW_EXPECT_TRUE( moved.isActive() );
    registration.reset(); // 빈 토큰의 해제는 칸을 건드리지 않는다
    SW_EXPECT_TRUE( slot.get() == &first );

    moved.reset();
    SW_EXPECT_NULL( slot.get() );

    auto secondRegistration = slot.bind( &second );
    moved.reset(); // 이미 빈 토큰 — 두 번 풀어도 새 값을 지우지 않는다
    SW_EXPECT_TRUE( slot.get() == &second );
    SW_EXPECT_FALSE( slot.bind( nullptr ).isActive() ); // nullptr 는 빈 토큰이고 단언도 아니다
    SW_EXPECT_TRUE( slot.get() == &second );
}

/**
 * @brief [ObserverSlotTest] 할당 계수기는 관찰자 칸을 쓰지 않는다 — 관찰자가 걸린 동안 계수기를 열고 닫아도 관찰자는 그대로 알림을 받는다
 */
SW_TEST_CASE( ObserverSlotTest, AllocationCounterLeavesObserverBound )
{
    using Counter = ObserverSlotAllocationCounterInternal;
    static constexpr sw::MemoryAllocationObserver kObserver{ &Counter::onAllocate, &Counter::onFree };
    Counter::s_allocateCount.store( 0 );

    sw::Memory::AllocationObserverRegistration registration = sw::Memory::registerAllocationObserver( &kObserver );
    {
        const sw::test::ScopedAllocationCount counter;
        void*                                 pBlock = sw::Memory::allocate( 64 );
        SW_REQUIRE_NOT_NULL( pBlock );
        sw::Memory::free( pBlock );
        if ( sw::test::ScopedAllocationCount::kCountsAreActive )
            SW_EXPECT_EQUAL( uint64( 1 ), counter.getCount() );
    }
    void* pAfter = sw::Memory::allocate( 64 );
    SW_REQUIRE_NOT_NULL( pAfter );
    sw::Memory::free( pAfter );
    registration.reset();

    if ( sw::test::ScopedAllocationCount::kCountsAreActive )
        SW_EXPECT_TRUE( Counter::s_allocateCount.load() >= 2u ); // 다른 스레드의 할당도 알림을 받으므로 하한만 본다
}
