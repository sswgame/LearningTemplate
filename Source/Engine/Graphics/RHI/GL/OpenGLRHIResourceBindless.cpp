/**
 * @file OpenGLRHIResourceBindless.cpp
 * @brief OpenGL 의 bindless 등록 — 리소스를 셰이더가 인덱스로 접근할 수 있게 올린다
 * @details `OpenGLRHIResource` 의 일부다. DX12/Vulkan 은 디스크립터 힙/배열에 쓰고, DX11/GL 은 슬롯
 *          기반이라 인덱스만 흉내 낸다 — 네 백엔드를 나란히 비교하기 좋은 지점이다.
 */
#include "pch.h"

#include "Core/Common/EnumUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

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

        const uint32 glName = _pDevice->getGLTextureName( texture );
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
        // 빈 슬롯(텍스처 인덱스가 잘못 넘어왔거나 이중 해제)을 다시 넣으면 같은 인덱스가 두 버퍼에 발급된다.
        if ( index < _pDevice->_listRegisteredBindless.size() && _pDevice->_listRegisteredBindless[index]._buffer == 0 )
        {
            SW_LOG_ERROR( "Bindless buffer index %# is already free; ignoring the duplicate release.", index );
            return;
        }
        releaseFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree, index,
                              OpenGLRHIDevice::BindlessResourceRecord{} );
    }

    void OpenGLRHIResource::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        if ( index < _pDevice->_listRegisteredTexture.size() && _pDevice->_listRegisteredTexture[index]._texture == 0 )
        {
            SW_LOG_ERROR( "Bindless texture index %# is already free; ignoring the duplicate release.", index );
            return;
        }
        releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, index,
                              OpenGLRHIDevice::BindlessTextureRecord{} );
    }

    RHIDescriptorIndex OpenGLRHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        GLuint ssbo = _pDevice->resolveGlBuffer( buffer );
        if ( ssbo == 0 )
            return kInvalidDescriptorIndex;
        return allocateFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUavFree,
                                      OpenGLRHIDevice::BindlessResourceRecord{ buffer } );
    }

    void OpenGLRHIResource::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        releaseFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUavFree, index,
                              OpenGLRHIDevice::BindlessResourceRecord{} );
    }
} // namespace sw
