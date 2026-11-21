#include "pch.h"

#include "Engine/Graphics/RHI/RHIFence.h"

#include "Core/Delegate/Delegate.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    RHIFence::RHIFence()
        : _pCompletedValue{ make_shared<atomic<uint64>>( uint64{ 0 } ) }
    {
    }

    uint64 RHIFence::signal( IRHIDevice& device )
    {
        const uint64                     fenceValue      = ++_lastSignalValue;
        const shared_ptr<atomic<uint64>> pCompletedValue = _pCompletedValue;
        // `Abandoned`(못 기다린 비우기)는 완료 값을 올리지 않는다 — 뒤 프레임의 `Completed` 가 그 값을 덮어 올린다(큐 순서).
        device.notifyFrameCompletion( RHIFrameCompletionDelegate::create( [pCompletedValue, fenceValue]( RHIFrameCompletion result )
        {
            if ( result == RHIFrameCompletion::Completed )
                (void)pCompletedValue->fetch_max( fenceValue, std::memory_order_acq_rel );
        } ) );
        return fenceValue;
    }
} // namespace sw
