#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

#include <glad/glad.h>

namespace sw
{
    OpenGLRHICommandContext::OpenGLRHICommandContext( OpenGLRHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _pState{ pDevice != nullptr ? &pDevice->_recordingState : nullptr }
    {
    }

    OpenGLRHICommandContext::OpenGLRHICommandContext( OpenGLRHIDevice* pDevice, OpenGLRecordingState* pState )
        : _pDevice{ pDevice }
        , _pState{ pState }
    {
    }

    static GLenum toGlPrimitive( RHIPrimitiveTopology topology )
    {
        switch ( topology )
        {
            case RHIPrimitiveTopology::TriangleList:
                return GL_TRIANGLES;
            case RHIPrimitiveTopology::LineList:
                return GL_LINES;
            case RHIPrimitiveTopology::PointList:
                return GL_POINTS;
            default:
                return GL_TRIANGLES;
        }
    }

    void OpenGLRHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || src == 0 )
            return;

        const OpenGLRHIDevice::OpenGLTextureRecord* pSrcRec = _pDevice->resolveTexture( src );
        if ( pSrcRec == nullptr || pSrcRec->_fbo == 0 || pSrcRec->_bDepthStencil != 0 )
            return;

        GLuint dstFbo{ 0 };
        uint32 dstW = _pDevice->_width;
        uint32 dstH = _pDevice->_height;
        if ( dst != 0 )
        {
            const OpenGLRHIDevice::OpenGLTextureRecord* pDstRec = _pDevice->resolveTexture( dst );
            if ( pDstRec == nullptr || pDstRec->_fbo == 0 || pDstRec->_bDepthStencil != 0 )
                return;
            dstFbo = pDstRec->_fbo;
            dstW   = pDstRec->_width;
            dstH   = pDstRec->_height;
        }

        glBindFramebuffer( GL_READ_FRAMEBUFFER, pSrcRec->_fbo );
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, dstFbo );
        glBlitFramebuffer( 0, 0, static_cast<GLint>( pSrcRec->_width ), static_cast<GLint>( pSrcRec->_height ),
                           0, 0, static_cast<GLint>( dstW ), static_cast<GLint>( dstH ),
                           GL_COLOR_BUFFER_BIT, GL_LINEAR );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    }

    void OpenGLRHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr )
            return;

        if ( pRecord->_program == 0 )
            return;

        _pDevice->_boundGraphicsPso = pso;
        glUseProgram( pRecord->_program );

        if ( pRecord->_vao != 0 )
            glBindVertexArray( pRecord->_vao );
        else if ( _pDevice->_vao != 0 )
            glBindVertexArray( _pDevice->_vao );

        glPolygonMode( GL_FRONT_AND_BACK, pRecord->_fillMode == RHIFillMode::Wireframe ? GL_LINE : GL_FILL );

        if ( pRecord->_cullMode == RHICullMode::None )
            glDisable( GL_CULL_FACE );
        else
        {
            glEnable( GL_CULL_FACE );
            glCullFace( pRecord->_cullMode == RHICullMode::Front ? GL_FRONT : GL_BACK );
            glFrontFace( GL_CW ); // match DirectX / clip-control path
        }

        if ( pRecord->_bEnableDepthTest )
        {
            glEnable( GL_DEPTH_TEST );
            glDepthFunc( GL_LESS );
            glDepthMask( pRecord->_bEnableDepthWrite ? GL_TRUE : GL_FALSE );
        }
        else
        {
            glDisable( GL_DEPTH_TEST );
            glDepthMask( GL_FALSE );
        }

        if ( pRecord->_bEnableBlend )
        {
            glEnable( GL_BLEND );
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        }
        else
            glDisable( GL_BLEND );
    }

    void OpenGLRHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr )
            return;

        if ( pRecord->_program != 0 )
            glUseProgram( pRecord->_program );
    }

    void OpenGLRHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;

        uint32 w = beginInfo._width > 0 ? beginInfo._width : _pDevice->_width;
        uint32 h = beginInfo._height > 0 ? beginInfo._height : _pDevice->_height;

        const bool       bBindColor = beginInfo._bBindColor != 0;
        const bool       bHasDepth  = beginInfo._depthTarget != 0;
        const uint32     colorCount = bBindColor ? ( beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u ) : 0u;
        RHITextureHandle colorHandles[kMaxColorAttachments]{};
        for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            colorHandles[attachmentIndex] = beginInfo._arrColorTarget[attachmentIndex];
        }

        bool bDepthOnly = ( bBindColor == false );
        if ( bDepthOnly == false && colorCount == 1 && colorHandles[0] != 0 )
        {
            const OpenGLRHIDevice::OpenGLTextureRecord* pColorRec = _pDevice->resolveTexture( colorHandles[0] );
            if ( pColorRec != nullptr && pColorRec->_bDepthStencil != 0 )
                bDepthOnly = true;
        }

        GLuint fbo{ 0 };
        if ( bBindColor && bDepthOnly == false && colorCount > 0 && colorHandles[0] != 0 )
            fbo = _pDevice->ensureCompositeFboMRT( colorHandles, colorCount, beginInfo._depthTarget );
        else if ( bBindColor == false && bHasDepth )
            fbo = _pDevice->ensureCompositeFboMRT( nullptr, 0, beginInfo._depthTarget );

        if ( fbo == 0 && colorCount == 1 && colorHandles[0] != 0 )
        {
            const OpenGLRHIDevice::OpenGLTextureRecord* pRec = _pDevice->resolveTexture( colorHandles[0] );
            if ( pRec != nullptr )
            {
                fbo        = pRec->_fbo;
                bDepthOnly = pRec->_bDepthStencil != 0;
                if ( beginInfo._width == 0 )
                    w = pRec->_width;
                if ( beginInfo._height == 0 )
                    h = pRec->_height;
            }
        }

        glBindFramebuffer( GL_FRAMEBUFFER, fbo );
        // bindShaderResource가 실제로 바인딩한 유닛만 언바인드한다(예전엔 0..15 전부 방어적으로 언바인드).
        const uint32 unbindMask = _pDevice->_boundTextureUnitMask;
        for ( uint32 unit = 0; unit < 32 && unbindMask != 0; ++unit )
        {
            if ( ( unbindMask & ( 1u << unit ) ) == 0 )
                continue;
            if ( glad_glBindTextureUnit != nullptr )
                glBindTextureUnit( unit, 0 );
            else
            {
                glActiveTexture( GL_TEXTURE0 + unit );
                glBindTexture( GL_TEXTURE_2D, 0 );
            }
        }
        _pDevice->_boundTextureUnitMask = 0;
        glViewport( 0, 0, static_cast<GLsizei>( w ), static_cast<GLsizei>( h ) );

        if ( bHasDepth || bDepthOnly )
        {
            glEnable( GL_DEPTH_TEST );
            glDepthMask( GL_TRUE );
        }
        else
        {
            glDisable( GL_DEPTH_TEST );
            glDepthMask( GL_FALSE );
        }

        if ( bDepthOnly == false )
        {
            glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
            if ( fbo != 0 )
            {
                if ( colorCount > 1 )
                {
                    GLenum drawBuffers[kMaxColorAttachments]{};
                    for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount; ++attachmentIndex )
                    {
                        drawBuffers[attachmentIndex] = GL_COLOR_ATTACHMENT0 + attachmentIndex;
                    }
                    glDrawBuffers( static_cast<GLsizei>( colorCount ), drawBuffers );
                }
                else
                    glDrawBuffer( GL_COLOR_ATTACHMENT0 );
            }
            else
                glDrawBuffer( GL_BACK );

            for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount; ++attachmentIndex )
            {
                const RHIRenderPassLoadOp loadOp = beginInfo._arrLoadOp[attachmentIndex];
                const float32*            pClear = &beginInfo._arrClearColor[attachmentIndex]._x;
                if ( loadOp != RHIRenderPassLoadOp::Clear )
                    continue;

                if ( fbo != 0 )
                {
                    GLenum drawBuf = GL_COLOR_ATTACHMENT0 + attachmentIndex;
                    glDrawBuffers( 1, &drawBuf );
                }
                else
                {
                    glDrawBuffer( GL_BACK );
                }
                glClearColor( pClear[0], pClear[1], pClear[2], pClear[3] );
                glClear( GL_COLOR_BUFFER_BIT );
            }
        }
        if ( ( bHasDepth || bDepthOnly ) && beginInfo._depthLoadOp == RHIRenderPassLoadOp::Clear )
        {
            glDepthMask( GL_TRUE );
            glClearDepth( static_cast<GLclampd>( beginInfo._clearDepth ) );
            glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
        }
    }

    void OpenGLRHICommandContext::endRenderPass()
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    }

    void OpenGLRHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        (void)buffer;
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;

        switch ( newState )
        {
            case RHIBufferState::IndirectArgument:
                glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT );
                break;
            case RHIBufferState::ShaderResource:
            case RHIBufferState::VertexOrConstant:
            case RHIBufferState::Index:
                glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
                break;
            case RHIBufferState::UnorderedAccess:
                glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT );
                break;
            case RHIBufferState::CopyDest:
                glMemoryBarrier( GL_BUFFER_UPDATE_BARRIER_BIT );
                break;
            case RHIBufferState::Common:
            default:
                break;
        }
    }

    void OpenGLRHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || index == kInvalidDescriptorIndex )
            return;

        if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() ) &&
             _pDevice->_listRegisteredUAV[index]._buffer != 0 )
        {
            GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredUAV[index]._buffer );
            if ( ssbo != 0 )
            {
                glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 48 + slot, ssbo );
                return;
            }
        }

        if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) &&
             _pDevice->_listRegisteredBindless[index]._buffer != 0 )
        {
            GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[index]._buffer );
            if ( ssbo != 0 )
            {
                glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 48 + slot, ssbo );
                return;
            }
        }
    }

    void OpenGLRHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || index == kInvalidDescriptorIndex )
            return;

        if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredTexture.size() ) &&
             _pDevice->_listRegisteredTexture[index]._texture != 0 )
        {
            const OpenGLRHIDevice::OpenGLTextureRecord* pRec = _pDevice->resolveTexture( _pDevice->_listRegisteredTexture[index]._texture );
            const GLuint                                tex  = pRec != nullptr ? pRec->_texture : 0;
            if ( tex != 0 )
            {
                glBindTextureUnit( slot, tex );
                if ( slot < 32 )
                    _pDevice->_boundTextureUnitMask |= ( 1u << slot );
                return;
            }
        }

        if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) &&
             _pDevice->_listRegisteredBindless[index]._buffer != 0 )
        {
            GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[index]._buffer );
            if ( ssbo != 0 )
            {
                glBindBufferBase( GL_SHADER_STORAGE_BUFFER, slot, ssbo );
                return;
            }
        }
    }

    void OpenGLRHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 bindConstantBuffer 와 같은 근거 — 명시 [[vk::binding]] 이 있으면 -fvk-b-shift 는 적용되지
        // 않으므로 b# 는 SPIR-V binding # 그대로다(gpucull 의 CullParams b0 = binding 0).
        if ( _pDevice->_bInitialized == SW_FALSE || index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        const GLuint ubo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[index]._buffer );
        if ( ubo != 0 )
            glBindBufferBase( GL_UNIFORM_BUFFER, slot, ubo );
    }

    void OpenGLRHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // HLSL t# 는 시프트 없이 그대로 GL SSBO 바인딩 #에 매핑된다 (gpucull 의 g_Instances 등).
        if ( _pDevice->_bInitialized == SW_FALSE || index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        const GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[index]._buffer );
        if ( ssbo != 0 )
            glBindBufferBase( GL_SHADER_STORAGE_BUFFER, slot, ssbo );
    }

    void OpenGLRHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        (void)slot;
        _pState->_boundMeshVb     = buffer;
        _pState->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
        _pState->_boundMeshOffset = offset;
    }

    void OpenGLRHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        _pState->_boundIndexBuffer = buffer;
        _pState->_boundIndexStride = ( indexStride == 2 ) ? 2u : 4u;
        _pState->_boundIndexOffset = offset;
    }

    void OpenGLRHICommandContext::bindPassAndMaterialUbo( RHIDescriptorIndex passCbDescriptorIndex,
                                                          RHIDescriptorIndex materialCbDescriptorIndex )
    {
        // 바인딩 번호는 구운 SPIR-V 의 OpDecorate 와 같아야 한다 — b0=binding 0, b1=binding 1.
        defaultBindPassAndMaterialCb( passCbDescriptorIndex, materialCbDescriptorIndex,
                                      _pDevice->_listRegisteredBindless.size(), 0 /*b0=PassCB*/, 1 /*b1=MaterialCB*/,
                                      [this]( RHIDescriptorIndex index, uint32 binding )
        {
            const GLuint ubo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[index]._buffer );
            if ( ubo != 0 )
                glBindBufferBase( GL_UNIFORM_BUFFER, binding, ubo );
        } );
    }

    void OpenGLRHICommandContext::bindMeshVaoAttribs( uint32 vbo )
    {
        const GLsizei stride = static_cast<GLsizei>( _pState->_boundMeshStride );
        glBindVertexArray( _pDevice->_meshVao );
        glBindBuffer( GL_ARRAY_BUFFER, vbo );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast<const void*>( static_cast<uintptr_t>( _pState->_boundMeshOffset + SW_OFFSET_OF( RHIVertex, _arrPosition ) ) ) );
        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast<const void*>( static_cast<uintptr_t>( _pState->_boundMeshOffset + SW_OFFSET_OF( RHIVertex, _arrColor ) ) ) );
    }

    void OpenGLRHICommandContext::draw( uint32 vertexCount, uint32 startVertex,
                                        RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || vertexCount == 0 )
            return;

        GLuint program = _pDevice->_shaderProgram;
        GLenum mode    = GL_TRIANGLES;

        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
        if ( pPso != nullptr )
        {
            if ( pPso->_program != 0 )
            {
                program = pPso->_program;
                mode    = toGlPrimitive( pPso->_topology );
            }
        }

        if ( program == 0 )
            return;

        glUseProgram( program );

        bindPassAndMaterialUbo( passCbDescriptorIndex, materialCbDescriptorIndex );

        if ( _pState->_boundMeshVb != 0 )
        {
            const GLuint vbo = _pDevice->resolveGlBuffer( _pState->_boundMeshVb );
            if ( vbo != 0 && _pDevice->_meshVao != 0 )
            {
                bindMeshVaoAttribs( vbo );
                glDrawArrays( mode, static_cast<GLint>( startVertex ), static_cast<GLsizei>( vertexCount ) );
                glBindVertexArray( 0 );
                glBindBuffer( GL_ARRAY_BUFFER, 0 );
            }
        }
        else if ( _pDevice->_vao != 0 )
        {
            glBindVertexArray( _pDevice->_vao );
            glDrawArrays( mode, static_cast<GLint>( startVertex ), static_cast<GLsizei>( vertexCount ) );
            glBindVertexArray( 0 );
        }
    }

    void OpenGLRHICommandContext::drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        // startInstance 는 셰이더 오프셋(g_InstanceBase)으로 넘기므로 여기선 무시한다 (GL 3.1 호환 glDrawArraysInstanced).
        (void)startInstance;
        if ( _pDevice->_bInitialized == SW_FALSE || vertexCount == 0 || instanceCount == 0 )
            return;

        GLuint program = _pDevice->_shaderProgram;
        GLenum mode    = GL_TRIANGLES;

        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
        if ( pPso != nullptr && pPso->_program != 0 )
        {
            program = pPso->_program;
            mode    = toGlPrimitive( pPso->_topology );
        }

        if ( program == 0 )
            return;

        glUseProgram( program );

        if ( _pState->_boundMeshVb != 0 )
        {
            const GLuint vbo = _pDevice->resolveGlBuffer( _pState->_boundMeshVb );
            if ( vbo != 0 && _pDevice->_meshVao != 0 )
            {
                bindMeshVaoAttribs( vbo );
                glDrawArraysInstanced( mode, static_cast<GLint>( startVertex ), static_cast<GLsizei>( vertexCount ),
                                       static_cast<GLsizei>( instanceCount ) );
                glBindVertexArray( 0 );
                glBindBuffer( GL_ARRAY_BUFFER, 0 );
            }
        }
        else if ( _pDevice->_vao != 0 )
        {
            glBindVertexArray( _pDevice->_vao );
            glDrawArraysInstanced( mode, static_cast<GLint>( startVertex ), static_cast<GLsizei>( vertexCount ),
                                   static_cast<GLsizei>( instanceCount ) );
            glBindVertexArray( 0 );
        }
    }

    void OpenGLRHICommandContext::bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot )
    {
        // 상수버퍼는 HLSL b# 가 **SPIR-V binding #** 로 그대로 나온다.
        // `-fvk-b-shift 16 0` 은 명시 `[[vk::binding]]` 이 있으면 적용되지 않는데(common.hlsli 가 항상 명시한다),
        // 엔진만 16+# 에 걸고 있었다. 그래서 셰이더가 PassCB 를 영영 못 읽어 g_ViewProj 가 0 이었고
        // OpenGL 은 메시를 하나도 그리지 못했다(드로우는 정상적으로 나가고 GL 에러도 없어 오래 걸렸다).
        // 확인 방법: 구운 .spv 의 OpDecorate 를 읽으면 `PassCB DescriptorSet 0 Binding 0` 이 그대로 보인다.
        if ( _pDevice->_bInitialized == SW_FALSE || cb == kInvalidDescriptorIndex ||
             cb >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        const GLuint ubo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[cb]._buffer );
        if ( ubo != 0 )
        {
            glBindBufferBase( GL_UNIFORM_BUFFER, slot, ubo );
        }
    }

    void OpenGLRHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // GL 은 t# SRV 버퍼도 bindShaderResource 와 동일 경로(SSBO/텍스처)로 처리한다.
        bindShaderResource( index, slot );
    }

    void OpenGLRHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;

        GLuint                                            program = _pDevice->_shaderProgram;
        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso    = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
        if ( pPso != nullptr && pPso->_program != 0 )
            program = pPso->_program;

        if ( program != 0 )
            glUseProgram( program );

        glDispatchCompute( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void OpenGLRHICommandContext::setViewport( const RHIViewport& viewport )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;
        glViewport( static_cast<GLint>( viewport._x ),
                    static_cast<GLint>( viewport._y ),
                    static_cast<GLsizei>( viewport._width ),
                    static_cast<GLsizei>( viewport._height ) );
    }

    void OpenGLRHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || num32BitValues == 0 || pData == nullptr )
            return;
        if ( destOffsetIn32BitValues >= OpenGLRHIDevice::kMaxComputeRootConstantDwords )
            return;

        const uint32 maxCount = OpenGLRHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = ( num32BitValues > maxCount ) ? maxCount : num32BitValues;

        if ( _pDevice->ensureComputeRootConstantUbo() == false )
            return;

        Memory::copy( _pDevice->_arrComputeRootConstantShadow + destOffsetIn32BitValues, pData, static_cast<size_t>( count ) * sizeof( uint32 ) );

        if ( glad_glNamedBufferSubData != nullptr )
            glNamedBufferSubData( _pDevice->_computeRootConstantUbo, 0, static_cast<GLsizeiptr>( sizeof( _pDevice->_arrComputeRootConstantShadow ) ), _pDevice->_arrComputeRootConstantShadow );
        else
        {
            glBindBuffer( GL_UNIFORM_BUFFER, _pDevice->_computeRootConstantUbo );
            glBufferSubData( GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>( sizeof( _pDevice->_arrComputeRootConstantShadow ) ), _pDevice->_arrComputeRootConstantShadow );
            glBindBuffer( GL_UNIFORM_BUFFER, 0 );
        }
        glBindBufferBase( GL_UNIFORM_BUFFER, rootParameterIndex, _pDevice->_computeRootConstantUbo );
    }

    void OpenGLRHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                                RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || argumentBuffer == 0 )
            return;

        GLuint program = _pDevice->_shaderProgram;
        GLenum mode    = GL_TRIANGLES;

        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
        if ( pPso != nullptr )
        {
            if ( pPso->_program != 0 )
            {
                program = pPso->_program;
                mode    = toGlPrimitive( pPso->_topology );
            }
        }

        if ( program == 0 )
            return;

        glUseProgram( program );

        bindPassAndMaterialUbo( passCbDescriptorIndex, materialCbDescriptorIndex );

        const GLuint vbo = _pDevice->resolveGlBuffer( _pState->_boundMeshVb );
        const GLuint buf = _pDevice->resolveGlBuffer( argumentBuffer );
        if ( vbo == 0 || _pDevice->_meshVao == 0 || buf == 0 || glad_glDrawArraysIndirect == nullptr )
            return;

        bindMeshVaoAttribs( vbo );

        glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );
        glDrawArraysIndirect( mode, reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) ) );
        glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );

        glBindVertexArray( 0 );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );
    }

    void OpenGLRHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || argumentBuffer == 0 )
            return;

        GLuint program = _pDevice->_shaderProgram;
        GLenum mode    = GL_TRIANGLES;

        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
        if ( pPso != nullptr )
        {
            if ( pPso->_program != 0 )
            {
                program = pPso->_program;
                mode    = toGlPrimitive( pPso->_topology );
            }
        }

        if ( program == 0 )
            return;

        glUseProgram( program );

        const GLuint vbo = _pDevice->resolveGlBuffer( _pState->_boundMeshVb );
        const GLuint ibo = _pDevice->resolveGlBuffer( _pState->_boundIndexBuffer );

        if ( vbo != 0 && _pDevice->_meshVao != 0 )
        {
            bindMeshVaoAttribs( vbo );
            if ( ibo != 0 )
                glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo );
        }

        GLuint buf = _pDevice->resolveGlBuffer( argumentBuffer );
        if ( buf != 0 && glad_glDrawElementsIndirect != nullptr )
        {
            const GLenum indexType = ( _pState->_boundIndexStride == 2 ) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
            glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );
            glDrawElementsIndirect( mode, indexType, reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) ) );
            glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
        }

        if ( vbo != 0 && _pDevice->_meshVao != 0 )
        {
            glBindVertexArray( 0 );
            glBindBuffer( GL_ARRAY_BUFFER, 0 );
            if ( ibo != 0 )
                glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
        }
    }

    void OpenGLRHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || argumentBuffer == 0 )
            return;

        GLuint buf = _pDevice->resolveGlBuffer( argumentBuffer );
        if ( buf == 0 )
            return;
        glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, buf );
        if ( glad_glDispatchComputeIndirect != nullptr )
            glDispatchComputeIndirect( static_cast<GLintptr>( argumentBufferOffset ) );
        glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, 0 );
    }

    void OpenGLRHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        // FBO 로 그린 결과를 텍스처로 샘플링하기 전에 필요한 배리어(예전 endOffscreenPass 가 하던 일).
        // FBO 0 재바인딩은 여기서 하지 않는다 — 다음 beginRenderPass 가 타깃을 명시적으로 정한다.
        (void)texture;
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;

        glMemoryBarrier( GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT );
    }

    void OpenGLRHICommandContext::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                                     uint32 maxCommandCount, RHIBufferHandle countBuffer,
                                                     uint32 countBufferOffset )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || argumentBuffer == 0 || maxCommandCount == 0 )
            return;

        const GLuint vbo = _pDevice->resolveGlBuffer( _pState->_boundMeshVb );
        if ( vbo != 0 && _pDevice->_meshVao != 0 )
        {
            bindMeshVaoAttribs( vbo );
        }

        GLuint buf = _pDevice->resolveGlBuffer( argumentBuffer );
        if ( buf != 0 )
        {
            glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );

            constexpr GLsizei stride  = sizeof( RHIDrawIndirectCommand );
            const void*       pOffset = reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) );

            if ( countBuffer != 0 && glad_glMultiDrawArraysIndirectCount != nullptr )
            {
                GLuint count = _pDevice->resolveGlBuffer( countBuffer );
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
                    const void* pCmdOffset = reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset + commandIndex * sizeof( RHIDrawIndirectCommand ) ) );
                    glDrawArraysIndirect( GL_TRIANGLES, pCmdOffset );
                }
            }

            glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
        }

        if ( _pDevice->_meshVao != 0 )
        {
            glBindVertexArray( 0 );
            glBindBuffer( GL_ARRAY_BUFFER, 0 );
        }
    }

    void OpenGLRHICommandContext::beginEventMarker( const utf8* pName )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || pName == nullptr )
            return;
        // OpenGL 4.3+ / 4.6 Core: KHR_debug
        glPushDebugGroup( GL_DEBUG_SOURCE_APPLICATION, 0, -1, pName );
    }

    void OpenGLRHICommandContext::endEventMarker()
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;
        glPopDebugGroup();
    }
} // namespace sw
