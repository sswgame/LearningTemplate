#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandList.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

namespace sw
{
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    OpenGLRHIDevice::OpenGLRHIDevice()
        : _pHDC{ nullptr }
        , _pHRC{ nullptr }
        , _pHWnd{ nullptr }
        , _width{ 1280 }
        , _height{ 720 }
        , _shaderProgram{ 0 }
        , _vao{ 0 }
        , _vbo{ 0 }
        , _meshVao{ 0 }
        , _defaultSampler{ 0 }
        , _defaultTexture{ 0 }
        , _gpuBuffers{}
        , _listRegisteredBindless{}
        , _listBindlessFree{}
        , _listRegisteredUAV{}
        , _listUavFree{}
        , _gpuTextures{}
        , _mapCompositeFbo{}
        , _listRegisteredTexture{}
        , _listTextureFree{}
        , _computeRootConstantUbo{ 0 }
        , _arrComputeRootConstantShadow{}
        , _pipelineStates{}
        , _listRenderPass{}
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
        , _boundGraphicsPso{ 0 }
        , _lastVsync{ -1 }
        , _bInitialized{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
        _resourceImpl = sw::make_unique<OpenGLRHIResource>( this );
    }

    OpenGLRHIDevice::~OpenGLRHIDevice()
    {
        shutdown();
    }

    IRHIResource*       OpenGLRHIDevice::getResource() { return _resourceImpl.get(); }
    IRHICommandContext* OpenGLRHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

    RHIBufferHandle OpenGLRHIDevice::createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride )
    {
        (void)indexStride;
        if ( _bInitialized == SW_FALSE || sizeBytes == 0 )
            return 0;

        GLuint ibo{ 0 };
        glGenBuffers( 1, &ibo );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo );
        glBufferData( GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeBytes ), pData, GL_STATIC_DRAW );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

        return storeGlBuffer( ibo );
    }

    void OpenGLRHIDevice::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                        RHIDescriptorIndex materialDescriptorIndex )
    {
        if ( _bInitialized == SW_FALSE || argumentBuffer == 0 )
            return;

        if ( materialDescriptorIndex != kInvalidDescriptorIndex &&
             materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _listRegisteredBindless.size() ) )
        {
            GLuint ubo = resolveGlBuffer( _listRegisteredBindless[materialDescriptorIndex]._buffer );
            if ( ubo != 0 )
                glBindBufferBase( GL_UNIFORM_BUFFER, 0, ubo );
        }

        GLuint buf = resolveGlBuffer( argumentBuffer );
        if ( buf == 0 )
            return;
        glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );

        if ( glad_glDrawArraysIndirect != nullptr )
            glDrawArraysIndirect( GL_TRIANGLES, reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) ) );
        glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
    }

    void OpenGLRHIDevice::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                             RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        if ( _bInitialized == SW_FALSE || argumentBuffer == 0 || maxCommandCount == 0 )
            return;

        GLuint buf = resolveGlBuffer( argumentBuffer );
        if ( buf == 0 )
            return;
        glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );

        constexpr GLsizei stride  = sizeof( RHIDrawIndirectCommand );
        const void*       pOffset = reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) );

        if ( countBuffer != 0 && glad_glMultiDrawArraysIndirectCount != nullptr )
        {
            GLuint count = resolveGlBuffer( countBuffer );
            if ( count != 0 )
                glBindBuffer( GL_PARAMETER_BUFFER, count );
            glMultiDrawArraysIndirectCount( GL_TRIANGLES, pOffset, static_cast<GLintptr>( countBufferOffset ),
                                            static_cast<GLsizei>( maxCommandCount ), stride );
            glBindBuffer( GL_PARAMETER_BUFFER, 0 );
        }
        else if ( glad_glMultiDrawArraysIndirect != nullptr )
            glMultiDrawArraysIndirect( GL_TRIANGLES, pOffset, static_cast<GLsizei>( maxCommandCount ), stride );
        else if ( glad_glDrawArraysIndirect != nullptr )
        {
            for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
            {
                const void* pCmdOffset = reinterpret_cast<const void*>( argumentBufferOffset + commandIndex * sizeof( RHIDrawIndirectCommand ) );
                glDrawArraysIndirect( GL_TRIANGLES, pCmdOffset );
            }
        }

        glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
    }

    bool OpenGLRHIDevice::ensureComputeRootConstantUbo()
    {
        if ( _computeRootConstantUbo != 0 )
            return true;
        if ( _bInitialized == SW_FALSE )
            return false;

        GLuint ubo{ 0 };
        if ( glad_glCreateBuffers != nullptr )
        {
            glCreateBuffers( 1, &ubo );
            glNamedBufferStorage( ubo, static_cast<GLsizeiptr>( sizeof( _arrComputeRootConstantShadow ) ), nullptr, GL_DYNAMIC_STORAGE_BIT );
        }
        else
        {
            glGenBuffers( 1, &ubo );
            glBindBuffer( GL_UNIFORM_BUFFER, ubo );
            glBufferData( GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>( sizeof( _arrComputeRootConstantShadow ) ), nullptr, GL_DYNAMIC_DRAW );
            glBindBuffer( GL_UNIFORM_BUFFER, 0 );
        }
        _computeRootConstantUbo = ubo;
        return _computeRootConstantUbo != 0;
    }

    uint32 OpenGLRHIDevice::ensureCompositeFbo( RHITextureHandle color, RHITextureHandle depth )
    {
        RHITextureHandle arrColor[1] = { color };
        return ensureCompositeFboMRT( arrColor, color != 0 ? 1u : 0u, depth );
    }

    uint32 OpenGLRHIDevice::ensureCompositeFboMRT( const RHITextureHandle* pColor, uint32 colorCount, RHITextureHandle depth )
    {
        CompositeFboKey key{};
        key._colorCount = colorCount > kMaxColorAttachments ? kMaxColorAttachments : colorCount;
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            key._arrColor[colorIndex] = pColor[colorIndex];
        }
        key._depth = depth;

        auto existing = _mapCompositeFbo.find( key );
        if ( existing != _mapCompositeFbo.end() )
            return existing->second;

        GLuint arrColorTex[kMaxColorAttachments]{};
        uint32 attachedColors{ 0 };
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            if ( key._arrColor[colorIndex] == 0 )
                continue;
            const OpenGLTextureRecord* pRec = resolveTexture( key._arrColor[colorIndex] );
            if ( pRec == nullptr || pRec->_bDepthStencil != 0 )
                return 0;
            arrColorTex[attachedColors++] = pRec->_texture;
        }

        GLuint depthTex{ 0 };
        if ( depth != 0 )
        {
            const OpenGLTextureRecord* pDepthRec = resolveTexture( depth );
            if ( pDepthRec == nullptr || pDepthRec->_bDepthStencil == 0 )
                return 0;
            depthTex = pDepthRec->_texture;
        }
        if ( attachedColors == 0 && depthTex == 0 )
            return 0;

        GLuint fbo{ 0 };
        glGenFramebuffers( 1, &fbo );
        glBindFramebuffer( GL_FRAMEBUFFER, fbo );
        GLenum arrDrawBuffer[kMaxColorAttachments]{};
        for ( uint32 colorIndex = 0; colorIndex < attachedColors; ++colorIndex )
        {
            glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colorIndex, GL_TEXTURE_2D, arrColorTex[colorIndex], 0 );
            arrDrawBuffer[colorIndex] = GL_COLOR_ATTACHMENT0 + colorIndex;
        }
        if ( attachedColors == 0 )
        {
            glDrawBuffer( GL_NONE );
            glReadBuffer( GL_NONE );
        }
        else
            glDrawBuffers( static_cast<GLsizei>( attachedColors ), arrDrawBuffer );

        if ( depthTex != 0 )
        {
            const OpenGLTextureRecord* pDepthRec       = resolveTexture( depth );
            const GLenum               depthAttachment = ( pDepthRec != nullptr && pDepthRec->_format == RHIFormat::D24_UNORM_S8_UINT )
                                                           ? GL_DEPTH_STENCIL_ATTACHMENT
                                                           : GL_DEPTH_ATTACHMENT;
            glFramebufferTexture2D( GL_FRAMEBUFFER, depthAttachment, GL_TEXTURE_2D, depthTex, 0 );
        }

        const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        if ( status != GL_FRAMEBUFFER_COMPLETE )
        {
            // status==0 은 "불완전" 이 아니라 **glCheckFramebufferStatus 자체가 실패했다**는 뜻이다 —
            // 실질적으로 현재 GL 컨텍스트가 없다는 신호다(GL 컨텍스트는 스레드 전용이고, 백엔드
            // 핫스왑은 Windows 에서 창과 컨텍스트를 통째로 재생성한다). 둘을 같은 문구로 찍으면
            // 첨부 포맷 문제인 줄 알고 엉뚱한 데를 파게 된다.
            if ( status == 0 )
                SW_LOG_WARNING( "Composite FBO 확인 실패 — 현재 GL 컨텍스트가 없습니다 "
                                "(스레드 바인딩 또는 백엔드 핫스왑 중 창/컨텍스트 재생성 확인)." );
            else
                SW_LOG_WARNING( "Composite FBO incomplete (status=%#).", static_cast<uint32>( status ) );
            glDeleteFramebuffers( 1, &fbo );
            return 0;
        }
        _mapCompositeFbo.emplace( key, fbo );
        return fbo;
    }

    uint32 OpenGLRHIDevice::resolveGlBuffer( RHIBufferHandle handle ) const
    {
        const uint32* pSlot = _gpuBuffers.get( handle );
        return pSlot != nullptr ? *pSlot : 0;
    }

    RHIBufferHandle OpenGLRHIDevice::storeGlBuffer( uint32 glName )
    {
        if ( glName == 0 )
            return 0;
        return _gpuBuffers.insert( glName );
    }

    OpenGLRHIDevice::OpenGLTextureRecord* OpenGLRHIDevice::resolveTexture( RHITextureHandle handle )
    {
        return _gpuTextures.get( handle );
    }

    const OpenGLRHIDevice::OpenGLTextureRecord* OpenGLRHIDevice::resolveTexture( RHITextureHandle handle ) const
    {
        return _gpuTextures.get( handle );
    }
} // namespace sw
