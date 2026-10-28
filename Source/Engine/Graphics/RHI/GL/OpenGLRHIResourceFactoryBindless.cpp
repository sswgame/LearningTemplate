/**
 * @file OpenGLRHIResourceFactoryBindless.cpp
 * @brief OpenGL 의 bindless 등록입니다. 리소스를 셰이더가 인덱스로 접근할 수 있게 등록합니다.
 * @details `OpenGLRHIResourceFactory` 의 일부입니다. DX12/Vulkan 은 디스크립터 힙과 배열을 쓰고, DX11/GL 은 슬롯
 *          기반이라 인덱스만 흉내 냅니다. 네 백엔드를 나란히 비교하기 좋은 곳입니다.
 *
 *          **등록 표는 GL 컨텍스트를 쥔 스레드만 읽고 쓴다.** 표에는 잠금이 없고(`_bindlessMutex` 가 없다), 드로우의 읽기는 렌더 스레드가 컨텍스트를 쥔 채 하며,
 *          `destroyBuffer` · `destroyTexture` 의 표 정리도 `ScopedOpenGLContext` 안에서 한다. 등록 · 해제도 같은 가드로 들어가 그 규칙을 지킨다
 *          (렌더 스레드가 프레임 중이면 프레임 끝까지 기다린다 — 바로 앞의 `createConstantBuffer` · `createTexture2D` 와 같다).
 */
#include "pch.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Foundation/EnumUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <glad/glad.h>

namespace sw
{

    SW_LOG_CALLER( "OpenGLRHIResourceFactory" );

    RHIDescriptorIndex OpenGLRHIResourceFactory::registerBindlessTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return kInvalidDescriptorIndex;

        ScopedOpenGLContext contextScope( _pDevice );
        const uint32        glName = _pDevice->getGlTextureName( texture );
        if ( glName == 0 )
            return kInvalidDescriptorIndex;
        // 셰이더의 텍스처 유닛은 sampler2D 로 선언돼 있다. 배열과 큐브는 바인딩할 수 없다(RHITextureDimension 참고).
        const OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord != nullptr && pRecord->_dimension != RHITextureDimension::Texture2D )
        {
            SW_LOG_ERROR( "registerBindlessTexture: texture %# is a %# texture — only Texture2D can be bound to shader texture units yet", texture,
                          static_cast<uint32>( pRecord->_dimension ) );
            return kInvalidDescriptorIndex;
        }
        // 끝나지 않은 업로드가 남은 텍스처는 등록하지 않는다(소비자는 pollTransfer 가 Complete 를 준 뒤에 등록한다 — IRHIResourceFactory 전송 계약 머리말).
        if ( pRecord != nullptr && _pDevice->_transferSerials.isTextureReadyToRegister( pRecord->_lastTransferSerial.load(), texture ) == false )
            return kInvalidDescriptorIndex;

        return allocateFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree,
                                      OpenGLRHIDevice::BindlessTextureRecord{ texture } );
    }

    RHIDescriptorIndex OpenGLRHIResourceFactory::registerBindlessBufferSRV( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        ScopedOpenGLContext contextScope( _pDevice );
        GLuint              ubo = _pDevice->resolveGlBuffer( buffer );
        if ( ubo == 0 )
            return kInvalidDescriptorIndex;
        return allocateFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree,
                                      OpenGLRHIDevice::BindlessResourceRecord{ buffer } );
    }

    void OpenGLRHIResourceFactory::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        ScopedOpenGLContext contextScope( _pDevice );
        // 이중 해제 가드는 `releaseFreeListIndex` 안에 있다.
        releaseFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree, index,
                              OpenGLRHIDevice::BindlessResourceRecord{} );
    }

    void OpenGLRHIResourceFactory::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        ScopedOpenGLContext contextScope( _pDevice );
        releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, index,
                              OpenGLRHIDevice::BindlessTextureRecord{} );
    }

    RHIDescriptorIndex OpenGLRHIResourceFactory::registerBindlessBufferUAV( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        ScopedOpenGLContext contextScope( _pDevice );
        GLuint              ssbo = _pDevice->resolveGlBuffer( buffer );
        if ( ssbo == 0 )
            return kInvalidDescriptorIndex;
        return allocateFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUAVFree,
                                      OpenGLRHIDevice::BindlessResourceRecord{ buffer } );
    }

    RHIDescriptorIndex OpenGLRHIResourceFactory::registerBindlessTextureUAV( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return kInvalidDescriptorIndex;

        ScopedOpenGLContext contextScope( _pDevice );
        if ( _pDevice->getGlTextureName( texture ) == 0 )
            return kInvalidDescriptorIndex;
        OpenGLRHIDevice::BindlessResourceRecord record{};
        record._texture = texture;
        return allocateFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUAVFree, record );
    }

    void OpenGLRHIResourceFactory::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        ScopedOpenGLContext contextScope( _pDevice );
        // 이중 해제는 도우미가 막는다. 막지 않으면 같은 인덱스가 프리리스트에 두 번 들어간다.
        releaseFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUAVFree, index,
                              OpenGLRHIDevice::BindlessResourceRecord{} );
    }
} // namespace sw
