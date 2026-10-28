#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHIDrawDiagnostics.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#include <glad/glad.h>

namespace sw
{
    namespace
    {
        GLenum toGlPrimitive( RHIPrimitiveTopology topology )
        {
            switch ( topology )
            {
                case RHIPrimitiveTopology::TriangleList:
                    return GL_TRIANGLES;
                case RHIPrimitiveTopology::LineList:
                    return GL_LINES;
                case RHIPrimitiveTopology::PointList:
                    return GL_POINTS;
            }
            return GL_TRIANGLES;
        }

        /**
         * @brief 범위 동안 GL_SCISSOR_TEST 를 끄고 끝나면 setScissorRect 가 걸어 둔 상태로 되돌립니다.
         * @details glBlitFramebuffer 는 가위를 따른다 — D3D · Vulkan 의 복사는 가위를 모르므로 blitTexture 는 가위 없이 옮긴다.
         */
        struct ScopedScissorTestOff
        {
            uint8 _bRestore;

            explicit ScopedScissorTestOff( const OpenGLRecordingState& state )
                : _bRestore{ state._bScissorEnabled }
            {
                if ( _bRestore == SW_TRUE )
                    glDisable( GL_SCISSOR_TEST );
            }
            ~ScopedScissorTestOff()
            {
                if ( _bRestore == SW_TRUE )
                    glEnable( GL_SCISSOR_TEST );
            }
            ScopedScissorTestOff( const ScopedScissorTestOff& )            = delete;
            ScopedScissorTestOff& operator=( const ScopedScissorTestOff& ) = delete;
        };
    } // namespace

    OpenGLRHICommandContext::OpenGLRHICommandContext( OpenGLRHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _pState{ pDevice != nullptr ? &pDevice->_recordingState : nullptr }
    {
    }

    void OpenGLRHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || ( src == 0 && dst == 0 ) )
            return;

        const ScopedScissorTestOff scissorOff{ _pDevice->_recordingState };
        if ( src == 0 )
        {
            // 창(기본 프레임버퍼) → 텍스처. 창의 0 행은 **아래**라 원본 y 를 뒤집어 읽는다 — 결과 텍스처의 0 행이 화면 위(D3D 와 같은 뜻)다.
            const OpenGLRHIDevice::OpenGLTextureRecord* pDstRecord                = _pDevice->resolveTexture( dst );
            const bool                                  bInvalidDestinationTarget = pDstRecord == nullptr || pDstRecord->_fbo == 0 || pDstRecord->_bDepthStencil != SW_FALSE;
            if ( bInvalidDestinationTarget )
                return;
            const GLint backWidth  = static_cast<GLint>( _pDevice->_width );
            const GLint backHeight = static_cast<GLint>( _pDevice->_height );
            glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
            glBindFramebuffer( GL_DRAW_FRAMEBUFFER, pDstRecord->_fbo );
            glBlitFramebuffer( 0, backHeight, backWidth, 0, 0, 0, static_cast<GLint>( pDstRecord->_width ), static_cast<GLint>( pDstRecord->_height ),
                               GL_COLOR_BUFFER_BIT, GL_NEAREST );
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
            return;
        }

        const OpenGLRHIDevice::OpenGLTextureRecord* pSrcRecord            = _pDevice->resolveTexture( src );
        const bool                                  bInvalidSourceTexture = pSrcRecord == nullptr || pSrcRecord->_fbo == 0 || pSrcRecord->_bDepthStencil != SW_FALSE;
        if ( bInvalidSourceTexture )
            return;

        GLuint dstFbo{ 0 };
        uint32 dstW = _pDevice->_width;
        uint32 dstH = _pDevice->_height;
        if ( dst != 0 )
        {
            const OpenGLRHIDevice::OpenGLTextureRecord* pDstRecord                = _pDevice->resolveTexture( dst );
            const bool                                  bInvalidDestinationRecord = pDstRecord == nullptr || pDstRecord->_fbo == 0 || pDstRecord->_bDepthStencil != SW_FALSE;
            if ( bInvalidDestinationRecord )
                return;
            dstFbo = pDstRecord->_fbo;
            dstW   = pDstRecord->_width;
            dstH   = pDstRecord->_height;
        }

        // 행 순서: FBO 는 UPPER_LEFT 로 그려 0 행이 화면 위다(beginRenderPass). 창(기본 프레임버퍼)은 0 행이 **아래**라,
        // 창으로 옮길 때는 대상 y 를 뒤집어야 화면에 바로 선다. 텍스처끼리는 행 순서가 같아 그대로 옮긴다.
        // 창 쪽 반전은 오프스크린을 읽는 스크린샷으로는 보이지 않는다 — RenderPassGPUTest.PresentedBackBufferMatchesTheCapture 가 창을 읽어 본다.
        const GLint dstY0 = dstFbo == 0 ? static_cast<GLint>( dstH ) : 0;
        const GLint dstY1 = dstFbo == 0 ? 0 : static_cast<GLint>( dstH );
        glBindFramebuffer( GL_READ_FRAMEBUFFER, pSrcRecord->_fbo );
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, dstFbo );
        glBlitFramebuffer( 0, 0, static_cast<GLint>( pSrcRecord->_width ), static_cast<GLint>( pSrcRecord->_height ),
                           0, dstY0, static_cast<GLint>( dstW ), dstY1,
                           GL_COLOR_BUFFER_BIT, GL_LINEAR );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    }

    void OpenGLRHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        // 등록되지 않은 핸들도 기록한다 — 앞 PSO 를 남겨 두면 다음 드로우가 그것으로 대신 그려진다. 그 드로우는 resolveDrawProgram 에서 멈춘다.
        _pDevice->_recordingState._boundGraphicsPSO               = pso;
        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || pRecord->_program == 0 )
            return;

        glUseProgram( pRecord->_program );

        if ( pRecord->_vao != 0 )
            glBindVertexArray( pRecord->_vao );
        else if ( _pDevice->_vao != 0 )
            glBindVertexArray( _pDevice->_vao );

        glPolygonMode( GL_FRONT_AND_BACK, pRecord->_fillMode == RHIFillMode::Wireframe ? GL_LINE : GL_FILL );

        // 깊이 바이어스 — 다른 백엔드와 같은 상수 · 기울기 두 칸이다(RHIPipelineStateDesc::_depthBias). 채우기 · 선 다각형 모드 모두에 건다.
        const bool bDepthBias = pRecord->_depthBias != 0 || pRecord->_slopeScaledDepthBias != 0.0f;
        if ( bDepthBias )
        {
            glEnable( GL_POLYGON_OFFSET_FILL );
            glEnable( GL_POLYGON_OFFSET_LINE );
            glPolygonOffset( pRecord->_slopeScaledDepthBias, static_cast<GLfloat>( pRecord->_depthBias ) );
        }
        else
        {
            glDisable( GL_POLYGON_OFFSET_FILL );
            glDisable( GL_POLYGON_OFFSET_LINE );
        }

        if ( pRecord->_cullMode == RHICullMode::None )
            glDisable( GL_CULL_FACE );
        else
        {
            glEnable( GL_CULL_FACE );
            glCullFace( pRecord->_cullMode == RHICullMode::Front ? GL_FRONT : GL_BACK );
            glFrontFace( GL_CW ); // DirectX · 클립 제어 경로와 같은 앞면 방향
        }

        if ( pRecord->_bEnableDepthTest )
        {
            glEnable( GL_DEPTH_TEST );
            glDepthFunc( GL_LEQUAL ); // DX11 · DX12 · Vulkan 과 같은 LessEqual — 깊이 프리패스 뒤 같은 깊이를 다시 그린다
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
            // 알파 채널은 One/InvSrcAlpha — DX11 · DX12 · Vulkan 과 같은 규칙이다(glBlendFunc 하나면 알파에도 SrcAlpha 가 곱해져 GL 만 알파가 달라진다).
            const GLenum colorSource = ( pRecord->_bPremultipliedAlpha == SW_TRUE ) ? GL_ONE : GL_SRC_ALPHA;
            glBlendFuncSeparate( colorSource, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
        }
        else
            glDisable( GL_BLEND );
    }

    void OpenGLRHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        // 등록되지 않은 핸들도 기록한다 — 앞 컴퓨트 PSO 를 남겨 두면 다음 디스패치가 그것으로 대신 돈다. 그 디스패치는 resolveComputeProgram 에서 멈춘다.
        _pDevice->_recordingState._boundComputePSO                = pso;
        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr )
            return;

        if ( pRecord->_program != 0 )
            glUseProgram( pRecord->_program );
    }

    // 길이 유지(긴 함수 정리 기준): 프레임버퍼 바인딩 · 드로우 버퍼 · 클리어 · 뷰포트의 순서가 곧 의미라 나누지 않는다.
    void OpenGLRHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;

        uint32 targetWidth  = beginInfo._width > 0 ? beginInfo._width : _pDevice->_width;
        uint32 targetHeight = beginInfo._height > 0 ? beginInfo._height : _pDevice->_height;

        const bool       bBindColor = beginInfo._bBindColor != SW_FALSE;
        const bool       bHasDepth  = beginInfo._depthTarget != 0;
        const uint32     colorCount = bBindColor ? ( beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u ) : 0u;
        RHITextureHandle colorHandles[kMaxColorAttachments]{};
        for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            colorHandles[attachmentIndex] = beginInfo._arrColorTarget[attachmentIndex];
        }

        bool       bDepthOnly         = ( bBindColor == false );
        const bool bSingleColorTarget = bDepthOnly == false && colorCount == 1 && colorHandles[0] != 0;
        if ( bSingleColorTarget )
        {
            const OpenGLRHIDevice::OpenGLTextureRecord* pColorRecord = _pDevice->resolveTexture( colorHandles[0] );
            if ( pColorRecord != nullptr && pColorRecord->_bDepthStencil != SW_FALSE )
                bDepthOnly = true;
        }

        // 프레임버퍼는 합성 FBO 하나로만 만든다. 만들지 못하면 텍스처별 FBO 로 대신하지 않고 멈춘다 — 대신하면 깊이 · 나머지 컬러 첨부가
        // 조용히 빠진 채 그려진다(다른 백엔드도 패스를 열지 못한 대상에는 그리지 않는다).
        GLuint     fbo{ 0 };
        const bool bUseColorComposite = bBindColor && bDepthOnly == false && colorCount > 0 && colorHandles[0] != 0;
        const bool bDepthAsColor      = bSingleColorTarget && bDepthOnly; // 깊이 텍스처를 컬러 자리로 건 경우 — 그 텍스처를 깊이 첨부로 쓴다
        if ( bUseColorComposite )
            fbo = _pDevice->ensureCompositeFboMrt( colorHandles, beginInfo._arrColorTargetSlice, colorCount, beginInfo._depthTarget, beginInfo._depthTargetSlice );
        else if ( bDepthAsColor )
        {
            fbo                                                 = _pDevice->ensureCompositeFboMrt( nullptr, nullptr, 0, colorHandles[0], beginInfo._arrColorTargetSlice[0] );
            const OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( colorHandles[0] );
            if ( pRecord != nullptr && beginInfo._width == 0 )
                targetWidth = pRecord->_width;
            if ( pRecord != nullptr && beginInfo._height == 0 )
                targetHeight = pRecord->_height;
        }
        else if ( bBindColor == false && bHasDepth )
            fbo = _pDevice->ensureCompositeFboMrt( nullptr, nullptr, 0, beginInfo._depthTarget, beginInfo._depthTargetSlice );

        const bool bOffscreenRequested = bUseColorComposite || bDepthAsColor || ( bBindColor == false && bHasDepth );
        if ( bOffscreenRequested && fbo == 0 )
            SW_FATAL( "GL render pass could not build its framebuffer (color0 %#, depth %#) - invalid attachment or incomplete FBO", colorHandles[0], beginInfo._depthTarget );

        glBindFramebuffer( GL_FRAMEBUFFER, fbo );

        // 클립 원점은 **대상에 따라 달라야 한다.**
        //  - FBO(오프스크린): GL_UPPER_LEFT. 텍스처 0 행이 화면 위가 되어 DX/Vulkan 과 행 순서가 같다.
        //    이래야 SceneColor 를 읽거나 다시 샘플링할 때 백엔드끼리 그림이 일치한다.
        //  - 기본 프레임버퍼(fbo 0): GL_LOWER_LEFT. 창에 보여줄 때 GL 은 y=0 을 **아래**로 표시한다.
        //    여기에도 UPPER_LEFT 를 걸면 NDC 위쪽이 창 아래로 가서 화면만 상하가 뒤집힌다.
        //    주의: 오프스크린 텍스처만 읽는 스크린샷 검증으로는 이 반전이 보이지 않는다(읽은 텍스처는 맞다).
        // 풀스크린 블릿의 uv 는 DX 규약(NDC 위쪽 = uv.y 0)이라, 위 조합이면 두 경로 모두 바로 선다.
        if ( glad_glClipControl != nullptr )
            glClipControl( fbo == 0 ? GL_LOWER_LEFT : GL_UPPER_LEFT, GL_ZERO_TO_ONE );
        // bindShaderResource 가 실제로 바인딩한 유닛만 언바인드한다.
        const uint32 unbindMask = _pDevice->_recordingState._boundTextureUnitMask;
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
        _pDevice->_recordingState._boundTextureUnitMask = 0;
        _pDevice->_recordingState._renderTargetHeight   = targetHeight;
        _pDevice->_recordingState._bDefaultFramebuffer  = fbo == 0 ? SW_TRUE : SW_FALSE;
        glViewport( 0, 0, static_cast<GLsizei>( targetWidth ), static_cast<GLsizei>( targetHeight ) );
        // 가위 = 패스 전체(IRHICommandList 규약). 아래 클리어보다 먼저다 — glClear 는 가위를 따른다(D3D · Vulkan 의 클리어는 따르지 않는다).
        glDisable( GL_SCISSOR_TEST );
        _pDevice->_recordingState._bScissorEnabled = SW_FALSE;

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

            // 첨부마다 **드로우 버퍼 상태를 건드리지 않고** 지운다.
            //
            // 주의: 지울 첨부를 고르려고 `glDrawBuffers` 로 목록을 하나로 좁히고 되돌리지 않으면, 드로우 버퍼가
            // "마지막으로 지운 첨부" 하나만 남아 이어지는 지오메트리의 `SV_TARGET0` 이 그 첨부로 가고 `SV_TARGET1` 은
            // 버려진다. G버퍼 패스에서는 알베도가 노멀 첨부에 써져 디퍼드 조명이 **OpenGL 만 거의 검게** 나온다.
            //
            // `glClearBufferfv` 는 드로우 버퍼 **인덱스**로 직접 지우므로 목록을 바꿀 이유가 없다.
            for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount; ++attachmentIndex )
            {
                const RHIRenderPassLoadOp loadOp = beginInfo._arrLoadOp[attachmentIndex];
                const float4&             clear  = beginInfo._arrClearColor[attachmentIndex];
                if ( loadOp != RHIRenderPassLoadOp::Clear )
                    continue;

                const GLfloat arrClearValue[4] = { clear._x, clear._y, clear._z, clear._w };
                glClearBufferfv( GL_COLOR, static_cast<GLint>( attachmentIndex ), arrClearValue );
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
        // 패스 밖에서는 **GL 기본 상태**로 되돌린다. 엔진 패스를 거치지 않고 기본 프레임버퍼에 직접 그리는
        // 코드(에디터 ImGui 백엔드가 그렇다)는 표준 GL 규약(좌하단 원점)을 가정한다. 오프스크린 패스가
        // 걸어 둔 UPPER_LEFT 가 남아 있으면 그쪽 UI 가 상하로 뒤집힌다.
        if ( glad_glClipControl != nullptr )
            glClipControl( GL_LOWER_LEFT, GL_ZERO_TO_ONE );
    }

    void OpenGLRHICommandContext::uavBarrier( RHIBufferHandle buffer )
    {
        // GL 에는 버퍼 단위 배리어가 없다. 비트로 어떤 접근을 기다릴지 고른다.
        (void)buffer;
        glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT );
    }

    void OpenGLRHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        (void)buffer;
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;

        switch ( newState )
        {
            case RHIBufferState::IndirectArgument:
            {
                glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT );
                break;
            }
            case RHIBufferState::ShaderResource:
            {
                glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
                break;
            }
            case RHIBufferState::VertexOrConstant:
            {
                // 정점 가져오기와 유니폼 읽기는 SSBO 배리어 비트로 덮이지 않는다(GL 사양 7.11.2) — Vulkan 의 VERTEX_ATTRIBUTE_READ · UNIFORM_READ 와 같은 접근이다.
                glMemoryBarrier( GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT );
                break;
            }
            case RHIBufferState::Index:
            {
                glMemoryBarrier( GL_ELEMENT_ARRAY_BARRIER_BIT );
                break;
            }
            case RHIBufferState::UnorderedAccess:
            {
                glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT );
                break;
            }
            case RHIBufferState::CopyDest:
            case RHIBufferState::CopySource:
            {
                glMemoryBarrier( GL_BUFFER_UPDATE_BARRIER_BIT );
                break;
            }
            case RHIBufferState::Common:
            {
                break;
            }
        }
    }

    void OpenGLRHICommandContext::copyBufferToReadback( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || sizeBytes == 0 )
            return;
        const GLuint sourceName = _pDevice->resolveGlBuffer( source );
        const GLuint targetName = _pDevice->resolveGlBuffer( readback );
        if ( sourceName == 0 || targetName == 0 )
            return;
        // 복사용 바인딩 지점을 쓴다 — SSBO · 인다이렉트 바인딩을 건드리지 않는다.
        glBindBuffer( GL_COPY_READ_BUFFER, sourceName );
        glBindBuffer( GL_COPY_WRITE_BUFFER, targetName );
        glCopyBufferSubData( GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, static_cast<GLsizeiptr>( sizeBytes ) );
        glBindBuffer( GL_COPY_READ_BUFFER, 0 );
        glBindBuffer( GL_COPY_WRITE_BUFFER, 0 );
    }

    void OpenGLRHICommandContext::bindComputeUav( RHIDescriptorIndex index, uint32 slot )
    {
        // 인덱스는 **UAV 레지스트리의 것**이다. 그 레지스트리는 RW 텍스처와 버퍼를 함께 담으므로 어느 쪽인지는
        // 레코드가 말해 준다. 다른 레지스트리를 넘겨짚지 않는다.
        //
        // 주의: UAV 레지스트리에서 못 찾았다고 **bindless(SRV) 레지스트리**로 흘러내리면 안 된다. 세 레지스트리가 각자
        // 0 부터 번호를 발급하므로 그 넘겨짚기는 언젠가 맞아떨어지고, 그때 **엉뚱한 버퍼가 엉뚱한
        // binding 에** 조용히 걸린다.
        if ( _pDevice->_bInitialized == SW_FALSE || index == kInvalidDescriptorIndex )
            return;

        if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() ) )
        {
            SW_LOG_WARNING( "bindComputeUav: index %# is not a registered UAV.", index );
            return;
        }

        const OpenGLRHIDevice::BindlessResourceRecord& record = _pDevice->_listRegisteredUAV[index];

        if ( record._texture != 0 )
        {
            // RW 텍스처. u5..u7 은 이미지 유닛(SW_GL_IMAGE_UNIT0 + 서수)이다.
            if ( slot < shaderslot::kComputeTextureUav0 || slot >= shaderslot::kComputeTextureUav0 + shaderslot::kComputeTextureUavSlotCount )
            {
                SW_LOG_WARNING( "bindComputeUav: tried to bind an RW texture to a buffer slot (u%#).", slot );
                return;
            }
            const OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( record._texture );
            if ( pRecord == nullptr || pRecord->_texture == 0 )
                return;
            glBindImageTexture( shaderslot::gl::kImageUnit0 + ( slot - shaderslot::kComputeTextureUav0 ), pRecord->_texture, 0,
                                GL_FALSE, 0, GL_READ_WRITE, pRecord->_internalFormat );
            return;
        }

        if ( record._buffer == 0 )
        {
            SW_LOG_WARNING( "bindComputeUav: the UAV record at index %# is empty.", index );
            return;
        }

        const GLuint ssbo = _pDevice->resolveGlBuffer( record._buffer );
        if ( ssbo == 0 )
            return;
        // u# → 계약 SSBO 번호. `-fvk-u-shift`(= SW_GL_UAV_BINDING0) 와 반드시 같아야 한다.
        glBindBufferBase( GL_SHADER_STORAGE_BUFFER, shaderslot::gl::kUavBinding0 + slot, ssbo );
    }

    void OpenGLRHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // **텍스처 전용이다.** 버퍼는 `bindStructuredBuffer` 가 건다.
        //
        // 주의: 텍스처 레지스트리에 없다고 버퍼 레지스트리로 흘러내리면 안 된다. 텍스처 · 버퍼 · UAV 는 **각자 0 부터
        // 시작하는 별개의 프리리스트**라 같은 번호가 셋 다 유효할 수 있고, 그러면 버퍼 인덱스가 그 번호의
        // **텍스처**로 걸린다(모프 정점 버퍼 t7 이 텍스처로 걸리면 정점 셰이더가 SSBO 7 에서 0 을 읽어
        // **OpenGL 에서만** 기하가 사라진다). 부르는 쪽은 종류를 이미 알고 그에 맞는 함수를 부른다.
        // 다른 세 백엔드도 추측하지 않는다(DX11 은 텍스처 레지스트리만 본다).
        if ( _pDevice->_bInitialized == SW_FALSE || index == kInvalidDescriptorIndex )
            return;

        if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredTexture.size() ) ||
             _pDevice->_listRegisteredTexture[index]._texture == 0 )
        {
            SW_LOG_WARNING( "bindShaderResource: index %# is not a registered texture - use bindStructuredBuffer for a buffer.", index );
            return;
        }

        const OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( _pDevice->_listRegisteredTexture[index]._texture );
        const GLuint                                tex     = pRecord != nullptr ? pRecord->_texture : 0;
        if ( tex == 0 )
            return;

        glBindTextureUnit( slot, tex );
        static_assert( shaderslot::kMaterialTexture0 + shaderslot::kMaterialTextureCount <= 32, "텍스처 유닛 마스크(uint32)가 머티리얼 텍스처 유닛을 다 담지 못한다" );
        if ( slot < 32 )
            _pDevice->_recordingState._boundTextureUnitMask |= ( 1u << slot );
    }

    void OpenGLRHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        // 그래픽스 bindConstantBuffer 와 같은 근거다. 명시 [[vk::binding]] 이 있으면 -fvk-b-shift 는 적용되지
        // 않으므로 b# 는 SPIR-V binding # 그대로다(gpucull 의 CullParams b0 = binding 0).
        const bool bInvalidConstantBufferIndex = _pDevice->_bInitialized == SW_FALSE || constantBufferIndex == kInvalidDescriptorIndex ||
                                                 constantBufferIndex >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() );
        if ( bInvalidConstantBufferIndex )
            return;
        const GLuint ubo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[constantBufferIndex]._buffer );
        if ( ubo != 0 )
            glBindBufferBase( GL_UNIFORM_BUFFER, slot, ubo );
    }

    void OpenGLRHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // HLSL t# 는 시프트 없이 그대로 GL SSBO 바인딩 # 에 매핑된다(gpucull 의 g_Instances 등).
        const bool bInvalidBufferIndex = _pDevice->_bInitialized == SW_FALSE || index == kInvalidDescriptorIndex ||
                                         index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() );
        if ( bInvalidBufferIndex )
            return;
        const GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[index]._buffer );
        if ( ssbo != 0 )
            glBindBufferBase( GL_SHADER_STORAGE_BUFFER, slot, ssbo );
    }

    void OpenGLRHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        // 슬롯 1 은 인스턴스 슬롯 스트림(uint, 인스턴스 스텝)이다. constant::arrVertexAttribute 의 SW_INSTANCESLOT.
        if ( slot == constant::kInstanceSlotStreamSlot )
        {
            _pState->_boundInstanceSlotVb     = buffer;
            _pState->_boundInstanceSlotOffset = offset;
            return;
        }
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

    void OpenGLRHICommandContext::bindMeshVaoAttribs( uint32 vbo )
    {
        const GLsizei stride = static_cast<GLsizei>( _pState->_boundMeshStride );
        glBindVertexArray( _pDevice->_meshVao );
        glBindBuffer( GL_ARRAY_BUFFER, vbo );
        // 정점 속성은 **공용 표**(constant::arrVertexAttribute)에서 건다. DX11 · DX12 · Vulkan 과 같은 표다.
        for ( uint32 attributeIndex = 0; attributeIndex < constant::kVertexAttributeCount; ++attributeIndex )
        {
            const RHIVertexAttribute& attribute = constant::arrVertexAttribute[attributeIndex];
            if ( attribute._inputSlot != 0 )
                continue; // 슬롯 1 은 아래에서 스트림 VBO 로
            glEnableVertexAttribArray( attribute._location );
            glVertexAttribPointer( attribute._location, static_cast<GLint>( attribute._componentCount ), GL_FLOAT, GL_FALSE, stride,
                                   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pState->_boundMeshOffset + attribute._byteOffset ) ) );
        }

        // 슬롯 1: 인스턴스 슬롯 스트림(uint, 인스턴스마다 하나). 안 걸린 드로우는 배열을 꺼 둔다. 켜 둔 채 버퍼가 없으면
        // 코어 프로파일에서 드로우가 거부된다.
        const GLuint streamVbo = _pDevice->resolveGlBuffer( _pState->_boundInstanceSlotVb );
        for ( uint32 attributeIndex = 0; attributeIndex < constant::kVertexAttributeCount; ++attributeIndex )
        {
            const RHIVertexAttribute& attribute = constant::arrVertexAttribute[attributeIndex];
            if ( attribute._inputSlot != constant::kInstanceSlotStreamSlot )
                continue;
            if ( streamVbo == 0 )
            {
                glDisableVertexAttribArray( attribute._location );
                continue;
            }
            glBindBuffer( GL_ARRAY_BUFFER, streamVbo );
            glEnableVertexAttribArray( attribute._location );
            glVertexAttribIPointer( attribute._location, static_cast<GLint>( attribute._componentCount ), GL_UNSIGNED_INT,
                                    static_cast<GLsizei>( constant::kInstanceSlotStreamStride ),
                                    reinterpret_cast<const void*>( static_cast<uintptr_t>( _pState->_boundInstanceSlotOffset + attribute._byteOffset ) ) );
            glVertexAttribDivisor( attribute._location, attribute._bPerInstance != SW_FALSE ? 1u : 0u );
        }
        glBindBuffer( GL_ARRAY_BUFFER, vbo );
    }

    void OpenGLRHICommandContext::resolveDrawProgram( uint32& outProgram, uint32& outMode ) const
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 그래픽스 PSO 없이 그리면 오류로 멈춘다. 기본 프로그램으로 대신 그리지 않는다 — PSO 가 0 이면
        // 부르는 쪽이 드로우를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · PSO 를 걸지 않은 드로우).
        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPSO = _pDevice->_pipelineStates.get( _pDevice->_recordingState._boundGraphicsPSO );
        if ( pPSO == nullptr || pPSO->_program == 0 )
            SW_FATAL( "GL draw without a registered graphics PSO (handle %#)", _pDevice->_recordingState._boundGraphicsPSO );
        outProgram = pPSO->_program;
        outMode    = toGlPrimitive( pPSO->_topology );
    }

    uint32 OpenGLRHICommandContext::resolveComputeProgram() const
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 컴퓨트 PSO 없이 디스패치하면 오류로 멈춘다. 조용히 건너뛰지 않고 그래픽스 프로그램으로도 돌지 않는다 — PSO 가 0 이면
        // 부르는 쪽이 디스패치를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · 컴퓨트 PSO 를 걸지 않은 디스패치).
        const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPSO = _pDevice->_pipelineStates.get( _pDevice->_recordingState._boundComputePSO );
        if ( pPSO == nullptr || pPSO->_program == 0 )
            SW_FATAL( "GL dispatch without a registered compute PSO (handle %#)", _pDevice->_recordingState._boundComputePSO );
        return pPSO->_program;
    }

    void OpenGLRHICommandContext::drawArrays( uint32 vertexCount, uint32 instanceCount, uint32 startVertex )
    {
        const bool bNothingToDraw = _pDevice->_bInitialized == SW_FALSE || vertexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw )
            return;

        GLuint program{ 0 };
        GLenum mode{ GL_TRIANGLES };
        resolveDrawProgram( program, mode );

        glUseProgram( program );

        const bool bMeshVertexBuffer = _pState->_boundMeshVb != 0;
        if ( bMeshVertexBuffer )
        {
            const GLuint vbo = _pDevice->resolveGlBuffer( _pState->_boundMeshVb );
            if ( vbo == 0 )
            {
                RHIDrawDiagnostics::reportDestroyedVertexBuffer( "OpenGL", _pState->_boundMeshVb );
                return;
            }
            if ( _pDevice->_meshVao == 0 )
                return;
            bindMeshVaoAttribs( vbo );
        }
        else
        {
            if ( _pDevice->_vao == 0 )
                return;
            glBindVertexArray( _pDevice->_vao );
        }

        if ( instanceCount == 1 )
            glDrawArrays( mode, static_cast<GLint>( startVertex ), static_cast<GLsizei>( vertexCount ) );
        else
            glDrawArraysInstanced( mode, static_cast<GLint>( startVertex ), static_cast<GLsizei>( vertexCount ), static_cast<GLsizei>( instanceCount ) );

        glBindVertexArray( 0 );
        if ( bMeshVertexBuffer )
            glBindBuffer( GL_ARRAY_BUFFER, 0 );
    }

    void OpenGLRHICommandContext::draw( uint32 vertexCount, uint32 startVertex )
    {
        drawArrays( vertexCount, 1, startVertex );
    }

    void OpenGLRHICommandContext::drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        // startInstance 는 무시한다(GL 3.1 호환 glDrawArraysInstanced 에는 시작 인스턴스 인자가 없다).
        // 인스턴스 자리가 필요한 씬 드로우는 drawIndirect 와 인스턴스 슬롯 스트림을 쓴다.
        (void)startInstance;
        drawArrays( vertexCount, instanceCount, startVertex );
    }

    void OpenGLRHICommandContext::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( _pDevice != nullptr )
            _pDevice->getResourceFactory()->updateConstantBuffer( buffer, pData, size );
    }

    void OpenGLRHICommandContext::bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        // 상수버퍼는 HLSL b# 가 **SPIR-V binding #** 로 그대로 나온다.
        // `-fvk-b-shift 16 0` 은 명시 `[[vk::binding]]` 이 있으면 적용되지 않는다(common.hlsli 가 항상 명시한다).
        // 주의: 엔진이 16+# 에 걸면 셰이더가 PassCB 를 못 읽어 g_ViewProj 가 0 이 되고, 드로우도 GL 에러도
        // 정상인 채로 메시가 하나도 그려지지 않는다.
        // 확인 방법: 쿠킹된 .spv 의 OpDecorate 를 읽으면 `PassCB DescriptorSet 0 Binding 0` 이 그대로 보인다.
        const bool bInvalidComputeConstantBufferIndex = _pDevice->_bInitialized == SW_FALSE || constantBufferIndex == kInvalidDescriptorIndex ||
                                                        constantBufferIndex >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() );
        if ( bInvalidComputeConstantBufferIndex )
            return;
        const GLuint ubo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[constantBufferIndex]._buffer );
        if ( ubo != 0 )
            glBindBufferBase( GL_UNIFORM_BUFFER, slot, ubo );
    }

    void OpenGLRHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // **버퍼 전용이다.** 텍스처는 `bindShaderResource` 가 건다(그쪽 주의 참고).
        // t# 는 시프트 없이 그대로 GL SSBO binding # 이다(common.hlsli SW_GL_BINDING).
        if ( _pDevice->_bInitialized == SW_FALSE || index == kInvalidDescriptorIndex )
            return;

        if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) ||
             _pDevice->_listRegisteredBindless[index]._buffer == 0 )
        {
            SW_LOG_WARNING( "bindStructuredBuffer: index %# is not a registered buffer - use bindShaderResource for a texture.", index );
            return;
        }

        const GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindless[index]._buffer );
        if ( ssbo != 0 )
            glBindBufferBase( GL_SHADER_STORAGE_BUFFER, slot, ssbo );
    }

    void OpenGLRHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;

        // 컴퓨트는 **컴퓨트 PSO** 의 프로그램으로 디스패치해야 한다. 그래픽스 PSO(또는 디바이스 기본 프로그램)로
        // 디스패치하면 GL_INVALID_OPERATION("no active compute program") 이 나는데, 컬링 결과가 CPU 가 채운
        // 인자 그대로 남아 화면에는 드러나지 않는다.
        glUseProgram( resolveComputeProgram() );
        glDispatchCompute( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
        // 이미지 스토어 결과를 이후 샘플링 · 읽기 · 업로드가 보도록 한다. SSBO 는 transitionBuffer 가 따로 막는다.
        glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT );
    }

    void OpenGLRHICommandContext::setViewport( const RHIViewport& viewport )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;
        // RHIViewport 는 왼쪽 위 원점이다(DX 규약). 기본 프레임버퍼는 glClipControl( GL_LOWER_LEFT )라 창 좌표가 아래 원점이므로 y 를 뒤집는다 —
        // 안 뒤집으면 화면 사각형 뷰(PiP · 분할 화면)가 위아래가 바뀐 자리에 그려진다. 오프스크린 FBO(GL_UPPER_LEFT)는 그대로다.
        const OpenGLRecordingState& state   = _pDevice->_recordingState;
        const float32               originY = ( state._bDefaultFramebuffer == SW_TRUE && state._renderTargetHeight > 0 )
                                                ? static_cast<float32>( state._renderTargetHeight ) - viewport._y - viewport._height
                                                : viewport._y;
        glViewport( static_cast<GLint>( viewport._x ),
                    static_cast<GLint>( originY ),
                    static_cast<GLsizei>( viewport._width ),
                    static_cast<GLsizei>( viewport._height ) );
        // 가위 = 뷰포트(IRHICommandList 규약). 삼각형은 이미 뷰포트로 잘리므로 가위 테스트를 끄는 것이 같은 결과다.
        glDisable( GL_SCISSOR_TEST );
        _pDevice->_recordingState._bScissorEnabled = SW_FALSE;
    }

    void OpenGLRHICommandContext::setScissorRect( const RHIScissorRect& rect )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;
        // 가위도 뷰포트와 같은 창 좌표다 — 기본 프레임버퍼(GL_LOWER_LEFT)는 y 를 아래 원점으로 뒤집고, 오프스크린 FBO(GL_UPPER_LEFT)는 그대로다.
        const OpenGLRecordingState& state   = _pDevice->_recordingState;
        const int64                 originY = ( state._bDefaultFramebuffer == SW_TRUE && state._renderTargetHeight > 0 )
                                                ? static_cast<int64>( state._renderTargetHeight ) - rect._y - rect._height
                                                : static_cast<int64>( rect._y );
        glEnable( GL_SCISSOR_TEST );
        glScissor( static_cast<GLint>( rect._x ), static_cast<GLint>( originY ), static_cast<GLsizei>( rect._width ), static_cast<GLsizei>( rect._height ) );
        _pDevice->_recordingState._bScissorEnabled = SW_TRUE;
    }

    void OpenGLRHICommandContext::setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        // UBO 바인딩 포인트는 스테이지 무관이라 컴퓨트 경로와 같은 호출이면 된다.
        setComputeRootConstants( rootParameterIndex, num32BitValues, pData, destOffsetIn32BitValues );
    }

    void OpenGLRHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        const bool bInvalidRootConstants = _pDevice->_bInitialized == SW_FALSE || num32BitValues == 0 || pData == nullptr;
        if ( bInvalidRootConstants )
            return;
        if ( destOffsetIn32BitValues >= shaderslot::kRootConstantDwords )
            return;

        const uint32 maxCount = shaderslot::kRootConstantDwords - destOffsetIn32BitValues;
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
        // 루트 상수는 계약 슬롯(b SW_SLOT_ROOT_CB_EMUL → UBO binding, SW_ROOT_CONSTANTS_BEGIN)에 건다.
        (void)rootParameterIndex;
        glBindBufferBase( GL_UNIFORM_BUFFER, shaderslot::kRootConstantEmulationSlot, _pDevice->_computeRootConstantUbo );
    }

    void OpenGLRHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount,
                                                RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        const bool bNothingToDrawIndirect = _pDevice->_bInitialized == SW_FALSE || argumentBuffer == 0 || drawCount == 0;
        if ( bNothingToDrawIndirect )
            return;

        GLuint program{ 0 };
        GLenum mode{ GL_TRIANGLES };
        resolveDrawProgram( program, mode );

        glUseProgram( program );

        // 메시를 걸지 않은 간접 드로우(GPU 입자 — SV_VertexID 로 정점을 만든다)는 drawArrays 와 같이 장치 공용 VAO 로 그린다(다른 세 백엔드도 공용 버퍼로 그린다).
        const bool   bMeshVertexBuffer = _pState->_boundMeshVb != 0;
        const GLuint vbo               = bMeshVertexBuffer ? _pDevice->resolveGlBuffer( _pState->_boundMeshVb ) : 0u;
        const GLuint buf               = _pDevice->resolveGlBuffer( argumentBuffer );
        if ( vbo == 0 && bMeshVertexBuffer )
            RHIDrawDiagnostics::reportDestroyedVertexBuffer( "OpenGL", _pState->_boundMeshVb );
        const bool bMissingMeshBuffers  = bMeshVertexBuffer && ( vbo == 0 || _pDevice->_meshVao == 0 );
        const bool bMissingSharedBuffer = bMeshVertexBuffer == false && _pDevice->_vao == 0;
        const bool bCannotDraw          = bMissingMeshBuffers || bMissingSharedBuffer || buf == 0;
        if ( bCannotDraw )
            return;

        if ( bMeshVertexBuffer )
            bindMeshVaoAttribs( vbo );
        else
            glBindVertexArray( _pDevice->_vao );
        glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );

        constexpr GLsizei stride  = sizeof( RHIDrawIndirectCommand );
        const void*       pOffset = reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) );

        if ( countBuffer != 0 && glad_glMultiDrawArraysIndirectCount != nullptr )
        {
            const GLuint count = _pDevice->resolveGlBuffer( countBuffer );
            if ( count != 0 )
                glBindBuffer( GL_PARAMETER_BUFFER, count );
            glMultiDrawArraysIndirectCount( mode, pOffset, static_cast<GLintptr>( countBufferOffset ),
                                            static_cast<GLsizei>( drawCount ), stride );
            glBindBuffer( GL_PARAMETER_BUFFER, 0 );
        }
        else if ( drawCount > 1 && glad_glMultiDrawArraysIndirect != nullptr )
            glMultiDrawArraysIndirect( mode, pOffset, static_cast<GLsizei>( drawCount ), stride );
        else if ( glad_glDrawArraysIndirect != nullptr )
        {
            for ( uint32 commandIndex = 0; commandIndex < drawCount; ++commandIndex )
            {
                const void* pCmdOffset =
                    reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset + commandIndex * stride ) );
                glDrawArraysIndirect( mode, pCmdOffset );
            }
        }

        glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
        glBindVertexArray( 0 );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );
    }

    void OpenGLRHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || argumentBuffer == 0 )
            return;

        GLuint program{ 0 };
        GLenum mode{ GL_TRIANGLES };
        resolveDrawProgram( program, mode );

        glUseProgram( program );

        const GLuint vbo = _pDevice->resolveGlBuffer( _pState->_boundMeshVb );
        const GLuint ibo = _pDevice->resolveGlBuffer( _pState->_boundIndexBuffer );
        // 건 메시 버퍼가 부서졌으면 드로우를 버린다(다른 드로우 진입점 · 세 백엔드와 같다).
        if ( vbo == 0 && _pState->_boundMeshVb != 0 )
        {
            RHIDrawDiagnostics::reportDestroyedVertexBuffer( "OpenGL", _pState->_boundMeshVb );
            return;
        }

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
        // dispatchCompute 와 같다 — 간접 디스패치도 **컴퓨트 PSO** 의 프로그램으로 한다. 앞서 그래픽스 PSO 가 건 프로그램이 남아 있으면
        // GL_INVALID_OPERATION("no active compute program")이 난다(D3D · Vulkan 은 컴퓨트 PSO 를 따로 걸어 이 차이가 없다).
        glUseProgram( resolveComputeProgram() );
        glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, buf );
        if ( glad_glDispatchComputeIndirect != nullptr )
            glDispatchComputeIndirect( static_cast<GLintptr>( argumentBufferOffset ) );
        glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, 0 );
        glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT );
    }

    void OpenGLRHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        // FBO 로 그린 결과를 텍스처로 샘플링하기 전에 필요한 배리어.
        // FBO 0 재바인딩은 여기서 하지 않는다. 다음 beginRenderPass 가 타깃을 명시적으로 정한다.
        (void)texture;
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;

        glMemoryBarrier( GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT );
    }

    void OpenGLRHICommandContext::prepareTextureForUnorderedAccess( RHITextureHandle texture )
    {
        // 상태 추적이 없다. 이전 렌더 · 샘플 결과가 이미지 스토어 전에 끝나도록 배리어만 친다.
        (void)texture;
        if ( _pDevice->_bInitialized == SW_FALSE )
            return;
        glMemoryBarrier( GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
    }

    void OpenGLRHICommandContext::writeBreadcrumb( uint32 markerId )
    {
        // OpenGL 은 컨텍스트를 잃은 뒤 GPU 가 닿은 자리를 읽을 길이 없다(`RHICapabilities::_bGpuBreadcrumbs` 거짓). 소실 보고는 CPU 가 마지막으로 기록한 패스를 싣는다.
        (void)markerId;
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
