/**
 * @file ScopedAllocationCount.h
 * @brief 스코프 동안 이 스레드의 sw 할당 횟수를 세는 RAII 가드입니다(할당 관찰자 `Memory::registerAllocationObserver`).
 * @details 배포본(SW_SHIPPING)에는 관찰자가 없어 `getCount()` 가 늘 0 이다 — 수를 보는 단언은 `kCountsAreActive` 로 가른다.
 *          관찰자는 하나뿐이라 가드를 겹쳐 걸지 않는다. 관찰자 안에서는 할당하지 않아야 해서 스레드 로컬 표지와 원자 수만 쓴다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw::test
{
    /**
     * @class ScopedAllocationCount
     * @brief 살아 있는 동안 이 스레드의 할당을 셉니다.
     */
    class ScopedAllocationCount
    {
    public:
        static constexpr bool kCountsAreActive = !build::kShipping;

        ScopedAllocationCount()
        {
            s_allocateCount.store( 0 );
            s_bCounting = true;
            Memory::registerAllocationObserver( &kObserver );
        }
        ~ScopedAllocationCount()
        {
            Memory::unregisterAllocationObserver();
            s_bCounting = false;
        }
        ScopedAllocationCount( const ScopedAllocationCount& )            = delete;
        ScopedAllocationCount& operator=( const ScopedAllocationCount& ) = delete;

        uint64 getCount() const { return s_allocateCount.load(); }

    private:
        static void onAllocate( const void*, size_t, MemoryTag )
        {
            if ( s_bCounting )
                s_allocateCount.fetch_add( 1 );
        }
        static void onFree( const void*, MemoryTag ) {}

        static inline thread_local bool s_bCounting = false;
        static inline atomic<uint64>    s_allocateCount{ 0 };

        static constexpr MemoryAllocationObserver kObserver{ &ScopedAllocationCount::onAllocate, &ScopedAllocationCount::onFree };
    };
} // namespace sw::test
