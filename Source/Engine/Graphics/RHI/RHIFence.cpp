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
        device.enqueueGPURelease( RHIResourceReleaseDelegate::create( [pCompletedValue, fenceValue]()
        { (void)pCompletedValue->fetch_max( fenceValue, std::memory_order_acq_rel ); } ) );
        return fenceValue;
    }
} // namespace sw
