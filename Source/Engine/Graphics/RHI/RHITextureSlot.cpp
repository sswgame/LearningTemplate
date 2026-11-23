#include "pch.h"

#include "Engine/Graphics/RHI/RHITextureSlot.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    bool RHITextureSlot::ensure( IRHIDevice& device, const RHITextureDesc& desc, const utf8* pDebugName )
    {
        const bool bWantsSRV  = desc._bIsShaderResource != 0;
        const bool bWantsUAV  = desc._bIsUnorderedAccess != 0;
        const bool bSameViews = ( _srv != kInvalidDescriptorIndex ) == bWantsSRV && ( _uav != kInvalidDescriptorIndex ) == bWantsUAV;
        if ( isValid() && bSameViews && _format == desc._format && _width == desc._width && _height == desc._height )
            return true;
        release( &device );
        IRHIResourceFactory& factory = *device.getResourceFactory();
        _texture                     = factory.createTexture2D( desc );
        if ( _texture == 0 )
            return false;
        factory.setTextureDebugName( _texture, pDebugName );
        if ( bWantsSRV )
            _srv = factory.registerBindlessTexture( _texture );
        if ( bWantsUAV )
            _uav = factory.registerBindlessTextureUAV( _texture );
        if ( ( bWantsSRV && _srv == kInvalidDescriptorIndex ) || ( bWantsUAV && _uav == kInvalidDescriptorIndex ) )
        {
            release( &device );
            return false;
        }
        _format = desc._format;
        _width  = desc._width;
        _height = desc._height;
        return true;
    }

    void RHITextureSlot::release( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr )
        {
            if ( _srv != kInvalidDescriptorIndex )
                pDevice->getResourceFactory()->unregisterBindlessTexture( _srv );
            if ( _uav != kInvalidDescriptorIndex )
                pDevice->getResourceFactory()->unregisterBindlessUAV( _uav );
            if ( _texture != 0 )
                pDevice->releaseHandle( _texture );
        }
        *this = RHITextureSlot{};
    }
} // namespace sw
