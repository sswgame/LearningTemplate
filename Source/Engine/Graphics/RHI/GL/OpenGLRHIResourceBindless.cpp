/**
 * @file OpenGLRHIResourceBindless.cpp
 * @brief OpenGL 의 bindless 등록입니다. 리소스를 셰이더가 인덱스로 접근할 수 있게 올립니다.
 * @details `OpenGLRHIResource` 의 일부입니다. DX12/Vulkan 은 디스크립터 힙 · 배열에 쓰고, DX11/GL 은 슬롯
 *          기반이라 인덱스만 흉내 냅니다. 네 백엔드를 나란히 비교하기 좋은 지점입니다.
 */
#include "pch.h"

#include "Core/Common/EnumUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <glad/glad.h>

namespace sw
{
    namespace
    {
    } // namespace

    SW_LOG_CALLER( "OpenGLRHIResource" );

    RHIDescriptorIndex OpenGLRHIResource::registerBindlessTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return kInvalidDescriptorIndex;

        const uint32 glName = _pDevice->getGlTextureName( texture );
        if ( glName == 0 )
            return kInvalidDescriptorIndex;

        return allocateFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree,
                                      OpenGLRHIDevice::BindlessTextureRecord{ texture } );
    }

    RHIDescriptorIndex OpenGLRHIResource::registerBindlessResource( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        GLuint ubo = _pDevice->resolveGlBuffer( buffer );
        if ( ubo == 0 )
            return kInvalidDescriptorIndex;
        return allocateFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree,
                                      OpenGLRHIDevice::BindlessResourceRecord{ buffer } );
    }

    void OpenGLRHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        // 이중 해제 가드는 `releaseFreeListIndex` 안에 있다. 종류 이름만 넘겨 로그를 맞춘다.
        releaseFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree, index,
                              OpenGLRHIDevice::BindlessResourceRecord{}, "buffer" );
    }

    void OpenGLRHIResource::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, index,
                              OpenGLRHIDevice::BindlessTextureRecord{}, "texture" );
    }

    RHIDescriptorIndex OpenGLRHIResource::registerBindlessUav( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        GLuint ssbo = _pDevice->resolveGlBuffer( buffer );
        if ( ssbo == 0 )
            return kInvalidDescriptorIndex;
        return allocateFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUavFree,
                                      OpenGLRHIDevice::BindlessResourceRecord{ buffer } );
    }

    RHIDescriptorIndex OpenGLRHIResource::registerBindlessTextureUav( RHITextureHandle texture )
    {
        if ( texture == 0 || _pDevice->getGlTextureName( texture ) == 0 )
            return kInvalidDescriptorIndex;
        OpenGLRHIDevice::BindlessResourceRecord record{};
        record._texture = texture;
        return allocateFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUavFree, record );
    }

    void OpenGLRHIResource::unregisterBindlessUav( RHIDescriptorIndex index )
    {
        // **여기가 원래 가드가 없던 자리다.** 이중 해제가 그대로 통과해 같은 인덱스가
        // 프리리스트에 두 번 들어갔다. 이제 도우미가 막는다.
        releaseFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUavFree, index,
                              OpenGLRHIDevice::BindlessResourceRecord{}, "uav" );
    }
} // namespace sw
