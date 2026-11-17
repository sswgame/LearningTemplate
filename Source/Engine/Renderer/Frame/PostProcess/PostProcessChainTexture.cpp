#include "pch.h"

#include "Engine/Renderer/Frame/PostProcess/PostProcessChainTexture.h"

#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    bool PostProcessChainTexture::ensure( IRHIDevice& device, RHIFormat format, uint32 width, uint32 height )
    {
        const bool bSame = _texture != 0 && _srv != kInvalidDescriptorIndex && _format == format && _width == width && _height == height;
        if ( bSame )
            return true;
        release( &device );
        IRHIResourceFactory* pFactory = device.getResourceFactory();
        RHITextureDesc       desc{};
        desc._width             = width;
        desc._height            = height;
        desc._format            = format;
        desc._bIsRenderTarget   = SW_TRUE; // 블릿 대상이다
        desc._bIsShaderResource = SW_TRUE;
        _texture                = pFactory->createTexture2D( desc );
        if ( _texture != 0 )
        {
            pFactory->setTextureDebugName( _texture, "PostProcessChain" );
            _srv = pFactory->registerBindlessTexture( _texture );
        }
        if ( _srv == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR_ONCE_ID( "PostProcessChainTexture.FailedCreate", "Failed to create the post process chain copy (%#x%#) - later materials at the same location read the original", width, height );
            release( &device );
            return false;
        }
        _format = format;
        _width  = width;
        _height = height;
        return true;
    }

    void PostProcessChainTexture::release( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr && _srv != kInvalidDescriptorIndex )
            pDevice->getResourceFactory()->unregisterBindlessTexture( _srv );
        if ( pDevice != nullptr && _texture != 0 )
            pDevice->getResourceFactory()->destroyTexture( _texture );
        _texture = 0;
        _srv     = kInvalidDescriptorIndex;
        _format  = RHIFormat::Unknown;
        _width   = 0;
        _height  = 0;
    }
} // namespace sw
