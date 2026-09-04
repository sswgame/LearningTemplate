#include "pch.h"

#include "Engine/Graphics/RenderPass/FrameResourceRegistry.h"

namespace sw
{
    void FrameResourceRegistry::reset()
    {
        _mapTexture.clear();
        _mapBuffer.clear();
    }

    void FrameResourceRegistry::registerTexture( hashed_string name, RHITextureHandle handle, RHIDescriptorIndex srv )
    {
        _mapTexture[name] = RegisteredTexture{ handle, srv };
    }

    void FrameResourceRegistry::registerBuffer( hashed_string name, RHIBufferHandle handle, RHIDescriptorIndex index )
    {
        _mapBuffer[name] = RegisteredBuffer{ handle, index };
    }

    const RegisteredTexture* FrameResourceRegistry::findTexture( hashed_string name ) const
    {
        auto it = _mapTexture.find( name );
        return it != _mapTexture.end() ? &it->second : nullptr;
    }

    const RegisteredBuffer* FrameResourceRegistry::findBuffer( hashed_string name ) const
    {
        auto it = _mapBuffer.find( name );
        return it != _mapBuffer.end() ? &it->second : nullptr;
    }
} // namespace sw
