#include "pch.h"

#include "Engine/Graphics/RHI/RHIVertexBufferBindings.h"

#include "Core/Log/LogOnce.h"

namespace sw
{
    void RHIVertexBufferBindings::set( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        if ( slot >= constant::render::kVertexBufferSlotCount )
        {
            SW_LOG_ERROR_ONCE_ID( "RHI.VertexBufferSlotOutOfRange", "setVertexBuffer: slot %# is out of range (%# slots) - ignored", slot, constant::render::kVertexBufferSlotCount );
            return;
        }
        _arrSlot[slot] = RHIVertexBufferBinding{ buffer, stride, offset };
    }

    void RHIVertexBufferBindings::forget( RHIBufferHandle buffer )
    {
        for ( RHIVertexBufferBinding& binding : _arrSlot )
        {
            if ( binding._buffer == buffer )
                binding = RHIVertexBufferBinding{};
        }
    }

    void RHIVertexBufferBindings::clear()
    {
        for ( RHIVertexBufferBinding& binding : _arrSlot )
        {
            binding = RHIVertexBufferBinding{};
        }
    }
} // namespace sw
