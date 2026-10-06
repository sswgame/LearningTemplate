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
        switch ( cause )
        {
            case ShutdownCause::None:
                return "None";
            case ShutdownCause::Interrupt:
                return "Interrupt";
            case ShutdownCause::Terminate:
                return "Terminate";
            case ShutdownCause::Hangup:
                return "Hangup";
            case ShutdownCause::ConsoleClose:
                return "ConsoleClose";
            case ShutdownCause::SystemShutdown:
                return "SystemShutdown";
            case ShutdownCause::ServiceStop:
                return "ServiceStop";
            case ShutdownCause::ConsoleCommand:
                return "ConsoleCommand";
            case ShutdownCause::TickLimit:
                return "TickLimit";
        }
        return "Unknown";
    }
} // namespace sw
