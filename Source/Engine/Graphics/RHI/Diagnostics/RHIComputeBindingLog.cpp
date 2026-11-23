#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIComputeBindingLog.h"

namespace sw
{
    void RHIComputeBindingLog::setEnabled( bool bEnabled )
    {
        _bEnabled.store( bEnabled ? SW_TRUE : SW_FALSE, std::memory_order_relaxed );
        if ( bEnabled )
            return;
        ScopedLock<mutex> lock{ _mutex };
        _mapBinding.clear();
    }

    void RHIComputeBindingLog::record( RHIDescriptorIndex index, bool bUAV, RHIPipelineStateHandle pso, uint32 slot )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapBinding.insert_or_assign( makeKey( index, bUAV ), Binding{ pso, slot } );
    }

    optional<RHIComputeBindingLog::Binding> RHIComputeBindingLog::find( RHIDescriptorIndex index, bool bUAV ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        it = _mapBinding.find( makeKey( index, bUAV ) );
        if ( it == _mapBinding.end() )
            return nullopt;
        return it->second;
    }
} // namespace sw
