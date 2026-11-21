#include "pch.h"

#include "Core/Concurrency/RealtimeSection.h"

#include "Core/Foundation/Macros.h"

namespace sw
{
#if defined( SW_DO_CHECKS )
    namespace
    {
        /** @brief 이 스레드에 걸린 실시간 구간의 셈 칸입니다(없으면 nullptr). */
        thread_local RealtimeViolationCounters* t_pRealtimeCounters{ nullptr };

        struct RealtimeSectionInternal
        {
            /** @brief 칸 @p counter 를 올리고, 단언 깃발이 켜져 있으면 멈춥니다. 단언 길이 다시 할당 · 잠금을 해도 세지 않게 구간을 먼저 뗀다. */
            static void note( DevCounter RealtimeViolationCounters::* pCounter )
            {
                RealtimeViolationCounters* const pCounters = t_pRealtimeCounters;
                if ( pCounters == nullptr )
                    return;
                ( pCounters->*pCounter ).add();
                if ( pCounters->_bAssertOnViolation.load( std::memory_order_relaxed ) )
                {
                    t_pRealtimeCounters = nullptr;
                    SW_LOW_LEVEL_ASSERT( false, "RealtimeSection: heap or lock inside a realtime section (audio device callback)" );
                }
            }
        };
    } // namespace
#endif

    RealtimeSection::RealtimeSection( [[maybe_unused]] RealtimeViolationCounters& counters )
    {
#if defined( SW_DO_CHECKS )
        _pOuter             = t_pRealtimeCounters;
        t_pRealtimeCounters = &counters;
#endif
    }

    RealtimeSection::~RealtimeSection()
    {
#if defined( SW_DO_CHECKS )
        t_pRealtimeCounters = _pOuter;
#endif
    }

    void RealtimeSection::recordAllocation()
    {
#if defined( SW_DO_CHECKS )
        RealtimeSectionInternal::note( &RealtimeViolationCounters::_allocationCount );
#endif
    }

    void RealtimeSection::recordFree()
    {
#if defined( SW_DO_CHECKS )
        RealtimeSectionInternal::note( &RealtimeViolationCounters::_freeCount );
#endif
    }

    void RealtimeSection::recordLock()
    {
#if defined( SW_DO_CHECKS )
        RealtimeSectionInternal::note( &RealtimeViolationCounters::_lockCount );
#endif
    }
} // namespace sw
