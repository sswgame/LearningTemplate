#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/FrameResourceRegistry.h"

namespace sw
{
    void FrameResourceRegistry::reset()
    {
        _mapTexture.clear();
        _mapBuffer.clear();
        ++_version;
    }

    void FrameResourceRegistry::registerTexture( hashed_string name, RHITextureHandle handle, RHIDescriptorIndex srv )
    {
        // **값이 실제로 달라질 때만** 버전을 올린다. 같은 값을 다시 등록하는 것은 상수버퍼 내용에 영향이 없고,
        // 그때마다 버전을 올리면 드로우마다 상수버퍼를 다시 만들게 된다(배치마다 같은 버퍼를 다시 등록하는 경로가 있다).
        RegisteredTexture& entry = _mapTexture[name];
        if ( entry._handle == handle && entry._srv == srv )
            return;
        entry = RegisteredTexture{ handle, srv };
        ++_version;
    }

    void FrameResourceRegistry::registerBuffer( hashed_string name, RHIBufferHandle handle, RHIDescriptorIndex index )
    {
        RegisteredBuffer& entry = _mapBuffer[name];
        if ( entry._handle == handle && entry._index == index )
            return;
        entry = RegisteredBuffer{ handle, index };
        ++_version;
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
