#include "pch.h"

#include "Engine/Graphics/RHI/RHIGeometryBuffer.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    bool RHIGeometryBuffer::createVertex( IRHIDevice& device, const void* pData, uint32 sizeBytes )
    {
        release( &device );
        _buffer = device.getResourceFactory()->createVertexBuffer( RHIResourceCreateKey{}, pData, sizeBytes );
        return isValid();
    }

    bool RHIGeometryBuffer::createGPUVertex( IRHIDevice& device, uint32 sizeBytes )
    {
        release( &device );
        _buffer = device.getResourceFactory()->createGPUVertexBuffer( RHIResourceCreateKey{}, sizeBytes );
        return isValid();
    }

    bool RHIGeometryBuffer::createIndex( IRHIDevice& device, const void* pData, uint32 sizeBytes )
    {
        release( &device );
        _buffer = device.getResourceFactory()->createIndexBuffer( RHIResourceCreateKey{}, pData, sizeBytes );
        return isValid();
    }

    void RHIGeometryBuffer::release( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr && _buffer != 0 )
            pDevice->releaseHandle( _buffer );
        _buffer = 0;
    }
} // namespace sw
