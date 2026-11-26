#include "pch.h"

#include "Core/Process/ShutdownSignal.h"

namespace sw
{
    static_assert( std::atomic<uint8>::is_always_lock_free, "ShutdownSignal is written from signal handlers - it needs a lock-free atomic" );

    atomic<uint8> ShutdownSignal::_s_requestedCause{ static_cast<uint8>( ShutdownCause::None ) };

    void ShutdownSignal::request( ShutdownCause cause )
    {
        uint8 expected = static_cast<uint8>( ShutdownCause::None );
        (void)_s_requestedCause.compare_exchange_strong( expected, static_cast<uint8>( cause ), std::memory_order_acq_rel, std::memory_order_acquire );
    }

    ShutdownCause ShutdownSignal::getRequestedCause()
    {
        return static_cast<ShutdownCause>( _s_requestedCause.load( std::memory_order_acquire ) );
    }

    const utf8* ShutdownSignal::getCauseName( ShutdownCause cause )
    {
        // 열거자 이름 그대로다(표 `ShutdownCauseList.xxx` 한 벌). 범위 밖 값은 "Unknown" 이다.
        static constexpr const utf8* kArrName[] = {
#define SW_SHUTDOWN_CAUSE( Name ) #Name,
#include "Core/Process/ShutdownCauseList.xxx"
#undef SW_SHUTDOWN_CAUSE
        };
        const size_t index = static_cast<size_t>( cause );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "Unknown";
    }
} // namespace sw
