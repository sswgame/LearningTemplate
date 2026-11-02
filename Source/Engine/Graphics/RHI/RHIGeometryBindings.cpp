#include "pch.h"

#include "Engine/Graphics/RHI/RHIGeometryBindings.h"

#include "Core/Log/LogOnce.h"

namespace sw
{
    void RHIGeometryBindings::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        if ( slot >= constant::render::kVertexBufferSlotCount )
        {
            SW_LOG_ERROR_ONCE_ID( "RHI.VertexBufferSlotOutOfRange", "setVertexBuffer: slot %# is out of range (%# slots) - ignored", slot, constant::render::kVertexBufferSlotCount );
            return;
        }
        _arrVertexSlot[slot] = RHIBufferBinding{ buffer, stride, offset };
    }

    void RHIGeometryBindings::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        _index = RHIBufferBinding{ buffer, indexStride == 2 ? 2u : 4u, offset };
    }

    void RHIGeometryBindings::clearVertexBuffers()
    {
        for ( RHIBufferBinding& binding : _arrVertexSlot )
        {
            binding = RHIBufferBinding{};
        }
    }

    void RHIGeometryBindings::forget( RHIBufferHandle buffer )
    {
        for ( RHIBufferBinding& binding : _arrVertexSlot )
        {
            if ( binding._buffer == buffer )
                binding = RHIBufferBinding{};
        }
        if ( _index._buffer == buffer )
            _index = RHIBufferBinding{ 0, 4, 0 };
    }
} // namespace sw
