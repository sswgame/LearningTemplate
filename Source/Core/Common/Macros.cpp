#include "pch.h"

#include "Core/Common/Macros.h"

#include <atomic>

namespace sw::internal
{
    namespace
    {
        /** @brief 걸린 가로채기 수 — 0 이면 단언이 멈춘다. */
        std::atomic<int32> s_assertCaptureDepth{ 0 };
        /** @brief 지금까지 가로챈 단언 수. */
        std::atomic<uint32> s_capturedAssertCount{ 0 };
    } // namespace

    bool tryCaptureAssert() noexcept
    {
        if ( s_assertCaptureDepth.load( std::memory_order_acquire ) <= 0 )
            return false;
        s_capturedAssertCount.fetch_add( 1, std::memory_order_acq_rel );
        return true;
    }

    void beginAssertCapture() noexcept { s_assertCaptureDepth.fetch_add( 1, std::memory_order_acq_rel ); }

    void endAssertCapture() noexcept { s_assertCaptureDepth.fetch_sub( 1, std::memory_order_acq_rel ); }

    uint32 getCapturedAssertCount() noexcept { return s_capturedAssertCount.load( std::memory_order_acquire ); }
} // namespace sw::internal
