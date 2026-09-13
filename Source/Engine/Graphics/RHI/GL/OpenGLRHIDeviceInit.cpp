#include "pch.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandList.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

#if !defined( SW_SHIPPING )
    namespace
    {
        /**
         * @brief KHR_debug 메시지를 엔진 로그로 보냅니다 — DX11/DX12 디버그 레이어(flushDebugMessages) · Vulkan 검증 레이어와 같은 자리.
         * @details 예전엔 GL 오류가 어디에도 나오지 않았다(glGetError 호출 0곳, 디버그 콜백 없음). 드로우가 정상으로 나가고
         *          화면만 비는 종류의 결함이 GL 에서 유독 오래 살아남은 이유다. 이제 스모크의 `[Error]` 수가 GL 에서도 뜻을 가진다.
         *          알림(NOTIFICATION)은 드라이버 잡담("Buffer detailed info")이라 glDebugMessageControl 로 아예 끈다.
         */
        void APIENTRY onOpenGLDebugMessage( GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* pMessage,
                                            const void* pUserParam )
        {
            (void)source;
            (void)length;
            (void)pUserParam;
            if ( pMessage == nullptr || severity == GL_DEBUG_SEVERITY_NOTIFICATION )
                return;
            if ( type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH )
                SW_LOG_ERROR( "GL debug %#: %#", static_cast<uint32>( id ), pMessage );
            else if ( severity == GL_DEBUG_SEVERITY_MEDIUM )
                SW_LOG_WARNING( "GL debug %#: %#", static_cast<uint32>( id ), pMessage );
            else
                SW_LOG_TRACE( "GL debug %#: %#", static_cast<uint32>( id ), pMessage );
        }
    } // namespace
#endif

    bool OpenGLRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        if ( _bInitialized == SW_TRUE )
            return true;

        _width  = desc._width;
        _height = desc._height;
        _pHWnd  = desc._pWindowHandle;

        // 플랫폼 컨텍스트 생성은 GL/Platform 이 안다. 예전에는 WGL·GLX·NSGL 세 갈래가 여기
        // 225줄로 들어앉아 있었다.
        _platformContext = IOpenGLPlatformContext::create();
        if ( _platformContext == nullptr )
            return false;

        OpenGLContextHandles contextHandles{};
        if ( _platformContext->initialize( desc, contextHandles ) == false )
        {
            _platformContext.reset();
            return false;
        }
        _pHDC = contextHandles._pDeviceContext;
        _pHRC = contextHandles._pRenderContext;

        if ( gladLoadGL() != 0 )
        {
            SW_LOG_INFO( "OpenGL glad Loaded Successfully." );

            // ZERO_TO_ONE = Direct3D 의 깊이 NDC [0,1]. 클립 **원점**은 대상마다 달라야 해서 여기서 고정하지 않는다 —
            // 오프스크린 FBO 는 UPPER_LEFT(행 순서를 DX 와 맞춤), 기본 프레임버퍼는 LOWER_LEFT(창 표시가 아래에서 위)로
            // OpenGLRHICommandContext::beginRenderPass 가 매 패스 정한다. 여기서는 가능 여부만 확인하고 기본값을 깐다.
            // **이 호출은 선택이 아니다.** 빠지면 GL 만 프레임버퍼 원점이 좌하단이라 SceneColor 의 행 순서가 다른 세
            // 백엔드와 반대로 쌓인다 — 풀스크린 블릿은 DX 규약(NDC 위쪽 = uv.y 0)을 백엔드 분기 없이 쓰므로 화면과
            // 스크린샷이 통째로 상하 반전된다. 깊이도 [-1,1] 로 남아 [0,1] 을 내보내는 투영이 버퍼의 절반만 쓴다.
            // 예전엔 이 블록이 `#ifdef GL_CLIP_CONTROL` 로 감싸여 있었다 — 그런 GL 토큰은 없다(실제 토큰은
            // GL_CLIP_ORIGIN / GL_CLIP_DEPTH_MODE 이고 GL_CLIP_CONTROL 은 함수 이름일 뿐이다). 그래서 4.6 컨텍스트에서도
            // 블록이 통째로 컴파일에서 빠져 한 번도 불리지 않았고, 로그도 남지 않아 오래 드러나지 않았다.
            // 이제 함수 포인터로 판단하고, 실제로 걸렸는지 GL 에 되물어 확인한다.
            if ( glad_glClipControl != nullptr )
            {
                glClipControl( GL_UPPER_LEFT, GL_ZERO_TO_ONE );

                GLint clipOrigin{ 0 };
                GLint clipDepth{ 0 };
                glGetIntegerv( GL_CLIP_ORIGIN, &clipOrigin );
                glGetIntegerv( GL_CLIP_DEPTH_MODE, &clipDepth );
                if ( clipOrigin != GL_UPPER_LEFT || clipDepth != GL_ZERO_TO_ONE )
                {
                    SW_LOG_ERROR( "glClipControl 이 적용되지 않았습니다 (origin %#, depth %#) — 화면이 상하 반전되고 깊이 정밀도가 절반이 됩니다.",
                                  static_cast<uint32>( clipOrigin ), static_cast<uint32>( clipDepth ) );
                }
                else
                {
                    SW_LOG_INFO( "OpenGL glClipControl 사용 가능 — 깊이 [0,1] 고정. 클립 원점은 대상마다 정한다(오프스크린 UPPER_LEFT / 기본 프레임버퍼 LOWER_LEFT)." );
                }
            }
            else
            {
                SW_LOG_ERROR( "glClipControl 을 쓸 수 없습니다 (GL 4.5 / ARB_clip_control 필요) — 화면이 상하 반전되고 깊이 정밀도가 절반이 됩니다." );
            }

#if !defined( SW_SHIPPING )
            // **GL 오류를 로그로.** 드라이버는 디버그 컨텍스트(WGL_CONTEXT_DEBUG_BIT_ARB)에서만 메시지를 만들 의무가 있다 —
            // 플랫폼 컨텍스트가 비-Shipping 빌드에서 그 비트를 켠다. SYNCHRONOUS 라 메시지가 원인 호출 안에서 나온다.
            if ( glad_glDebugMessageCallback != nullptr && glad_glDebugMessageControl != nullptr )
            {
                glEnable( GL_DEBUG_OUTPUT );
                glEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
                glDebugMessageCallback( &onOpenGLDebugMessage, nullptr );
                glDebugMessageControl( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE );
                SW_LOG_INFO( "OpenGL KHR_debug 출력을 켰습니다 — GL 오류가 오류 로그로 나옵니다 (스모크의 오류 수에 잡힌다)." );
            }
            else
                SW_LOG_WARNING( "glDebugMessageCallback 을 쓸 수 없습니다 (GL 4.3 / KHR_debug 필요) — GL 오류가 로그에 나오지 않습니다." );
#endif

            // **프로보킹 정점은 FIRST 다.** GL 기본은 LAST 인데 DirectX·Vulkan 은 FIRST 라, `nointerpolation`
            // 값이 삼각형의 **다른 정점**에서 온다. 지금 flat 으로 넘기는 값(materialIndex)은 배치 안에서 전부
            // 같아 증상이 없지만, 정점마다 다른 flat 값을 넘기는 날 GL 만 다른 그림을 낸다 — 백엔드 하나만
            // 조용히 다른 종류의 버그는 여기서 미리 막는다.
            if ( glad_glProvokingVertex != nullptr )
                glProvokingVertex( GL_FIRST_VERTEX_CONVENTION );
            else
                SW_LOG_WARNING( "glProvokingVertex 를 쓸 수 없습니다 — nointerpolation 값이 DX·Vulkan 과 다른 정점에서 옵니다." );

            // **정점 스테이지의 SSBO 한도를 실제로 물어본다.** GL 4.3 스펙이 요구하는 최소값은 0 이다 —
            // 정점 셰이더에서 구조버퍼를 읽는 것이 보장된 기능이 아니다. 이 엔진은 정점 셰이더에서
            // 인스턴스(t4)·가시 목록(t10)·모프 정점(t11)을 읽으므로, 한도가 그보다 작으면 링크는
            // 통과해도 읽기가 어긋난다. 화면으로는 "기하가 무너진다" 로만 보여서 원인을 짚기 어렵다.
            GLint maxVertexSsbo{ 0 };
            GLint maxComputeSsbo{ 0 };
            GLint maxFragmentSsbo{ 0 };
            glGetIntegerv( GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &maxVertexSsbo );
            glGetIntegerv( GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &maxComputeSsbo );
            glGetIntegerv( GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, &maxFragmentSsbo );
            SW_LOG_INFO( "OpenGL SSBO 한도: 정점 %#, 프래그먼트 %#, 컴퓨트 %#", static_cast<int32>( maxVertexSsbo ),
                         static_cast<int32>( maxFragmentSsbo ), static_cast<int32>( maxComputeSsbo ) );
            if ( maxVertexSsbo < static_cast<GLint>( shaderslot::kSrvSlotCount ) )
            {
                SW_LOG_WARNING( "정점 스테이지 SSBO 한도(%#)가 SRV 슬롯 수(%#)보다 작습니다 — 큰 번호의 슬롯은 읽기가 어긋날 수 있습니다.",
                                static_cast<int32>( maxVertexSsbo ), shaderslot::kSrvSlotCount );
            }
        }
        else
        {
            SW_LOG_WARNING( "gladLoadGL이 0을 반환함, OpenGL 디바이스 초기화 실패" );
            return false;
        }

        // 이 백엔드는 셰이더를 SPIR-V 로만 올린다(DXC → SPIR-V → glSpecializeShader). ARB_gl_spirv 가
        // 없으면 PSO 를 단 하나도 만들지 못해 "초기화는 성공했는데 화면이 텅 비는" 상태가 된다.
        // 그대로 두면 호출부가 이 백엔드를 멀쩡한 것으로 믿고 계속 쓴다 — 여기서 실패로 끊어야
        // RHI::createDevice 가 null 을 돌려주고 상위(테스트/렌더러)가 다른 백엔드로 넘어간다.
        // WSL 의 Mesa 드라이버(llvmpipe/d3d12)가 이 확장을 노출하지 않아 실제로 걸린다.
        if ( glad_glShaderBinary == nullptr || glad_glSpecializeShader == nullptr )
        {
            SW_LOG_WARNING( "OpenGL 백엔드를 쓸 수 없습니다 — GL_ARB_gl_spirv 없음(glShaderBinary/glSpecializeShader null)." );
            return false;
        }

        _bInitialized = SW_TRUE;

        {
            const RHIVertex arrFullscreenVert[3] = {
                // 화면 공간 삼각형이라 노멀은 쓰이지 않는다 — 레이아웃을 채우려고 +Z 를 둔다.
                // 셰이더는 SV_VertexID 로 UV 를 만들지만 레이아웃에 맞춰 같은 값을 실어 둔다.
                {{ -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },  { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
                { { 3.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },  { 2.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
                { { -1.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            };
            glGenVertexArrays( 1, &_vao );
            glGenBuffers( 1, &_vbo );
            glBindVertexArray( _vao );
            glBindBuffer( GL_ARRAY_BUFFER, _vbo );
            glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeof( arrFullscreenVert ) ), arrFullscreenVert, GL_STATIC_DRAW );
            for ( uint32 attributeIndex = 0; attributeIndex < constant::kVertexAttributeCount; ++attributeIndex )
            {
                const RHIVertexAttribute& attribute = constant::arrVertexAttribute[attributeIndex];
                glEnableVertexAttribArray( attribute._location );
                glVertexAttribPointer( attribute._location, static_cast<GLint>( attribute._componentCount ), GL_FLOAT, GL_FALSE,
                                       static_cast<GLsizei>( sizeof( RHIVertex ) ),
                                       reinterpret_cast<void*>( static_cast<uintptr_t>( attribute._byteOffset ) ) );
            }
            glBindVertexArray( 0 );
            glBindBuffer( GL_ARRAY_BUFFER, 0 );

            glGenVertexArrays( 1, &_meshVao );
            glBindVertexArray( _meshVao );
            for ( uint32 attributeIndex = 0; attributeIndex < constant::kVertexAttributeCount; ++attributeIndex )
                glEnableVertexAttribArray( constant::arrVertexAttribute[attributeIndex]._location );
            glBindVertexArray( 0 );

            GLuint defaultTex{ 0 };
            glGenTextures( 1, &defaultTex );
            glBindTexture( GL_TEXTURE_2D, defaultTex );
            const uint32 whitePixel = 0xFFFFFFFF;
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whitePixel );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
            glBindTexture( GL_TEXTURE_2D, 0 );
            _defaultTexture = defaultTex;

            GLuint defaultSampler{ 0 };
            glGenSamplers( 1, &defaultSampler );
            glSamplerParameteri( defaultSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
            glSamplerParameteri( defaultSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
            glSamplerParameteri( defaultSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glSamplerParameteri( defaultSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glSamplerParameteri( defaultSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE );
            for ( uint32 unit = 0; unit < 64; ++unit )
            {
                glBindTextureUnit( unit, defaultTex );
                glBindSampler( unit, defaultSampler );
            }
            _defaultSampler = defaultSampler;
        }

        _frameStreamContext = sw::make_unique<OpenGLRHICommandContext>( this );

        SW_LOG_INFO( "OpenGL RHI Backend Device Active (DirectX Coordinate System & Top-Left UV Texture Space Configured)" );
        return true;
    }

    void OpenGLRHIDevice::shutdownInternal()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // GL 자원을 지우기 전에 컨텍스트를 되찾는다 (플랫폼이 필요 없으면 아무것도 하지 않는다).
        if ( _platformContext != nullptr )
            _platformContext->reacquireForFrame();

        _releaseQueue.flushAll();
        _frameStreamContext.reset();

        if ( _defaultSampler )
        {
            for ( uint32 unit = 0; unit < 64; ++unit )
            {
                glBindSampler( unit, 0 );
                glBindTextureUnit( unit, 0 );
            }
            glDeleteSamplers( 1, &_defaultSampler );
            _defaultSampler = 0;
        }
        if ( _defaultTexture )
        {
            glDeleteTextures( 1, &_defaultTexture );
            _defaultTexture = 0;
        }
        if ( _shaderProgram )
        {
            glDeleteProgram( _shaderProgram );
            _shaderProgram = 0;
        }
        if ( _vao )
        {
            glDeleteVertexArrays( 1, &_vao );
            _vao = 0;
        }
        if ( _vbo )
        {
            glDeleteBuffers( 1, &_vbo );
            _vbo = 0;
        }
        if ( _meshVao )
        {
            glDeleteVertexArrays( 1, &_meshVao );
            _meshVao = 0;
        }
        _gpuBuffers.forEach( []( uint32& name )
        {
            if ( name != 0 )
            {
                GLuint glName = name;
                glDeleteBuffers( 1, &glName );
                name = 0;
            }
        } );
        _recordingState._boundMeshVb      = 0;
        _recordingState._boundMeshStride  = sizeof( RHIVertex );
        _recordingState._boundMeshOffset  = 0;
        _recordingState._boundIndexBuffer = 0;
        _recordingState._boundIndexStride = 4;
        _recordingState._boundIndexOffset = 0;
        _listRegisteredBindless.clear();
        _listRegisteredUAV.clear();
        _listRegisteredTexture.clear();
        _listTextureFree.clear();
        _listBindlessFree.clear();
        _listUavFree.clear();
        Memory::set( _arrComputeRootConstantShadow, 0, sizeof( _arrComputeRootConstantShadow ) );

        _pipelineStates.forEach( []( OpenGLPipelineStateRecord& pso )
        {
            if ( pso._program != 0 )
            {
                glDeleteProgram( pso._program );
                pso._program = 0;
            }
        } );
        _pipelineStates.clear();
        _listRenderPass.clear();

        _gpuBuffers.forEach( []( uint32& glBuf )
        {
            if ( glBuf != 0 )
            {
                glDeleteBuffers( 1, &glBuf );
                glBuf = 0;
            }
        } );
        _gpuBuffers.clear();

        _gpuTextures.forEach( []( OpenGLTextureRecord& rec )
        {
            if ( rec._texture != 0 )
            {
                glDeleteTextures( 1, &rec._texture );
                rec._texture = 0;
            }
            if ( rec._fbo != 0 )
            {
                glDeleteFramebuffers( 1, &rec._fbo );
                rec._fbo = 0;
            }
        } );
        _gpuTextures.clear();

        for ( auto& [key, fbo] : _mapCompositeFbo )
        {
            if ( fbo != 0 )
            {
                GLuint glFbo = fbo;
                glDeleteFramebuffers( 1, &glFbo );
            }
        }
        _mapCompositeFbo.clear();

        if ( _vao != 0 )
        {
            glDeleteVertexArrays( 1, &_vao );
            _vao = 0;
        }
        if ( _vbo != 0 )
        {
            glDeleteBuffers( 1, &_vbo );
            _vbo = 0;
        }
        if ( _meshVao != 0 )
        {
            glDeleteVertexArrays( 1, &_meshVao );
            _meshVao = 0;
        }
        if ( _shaderProgram != 0 )
        {
            glDeleteProgram( _shaderProgram );
            _shaderProgram = 0;
        }
        if ( _defaultSampler != 0 )
        {
            glDeleteSamplers( 1, &_defaultSampler );
            _defaultSampler = 0;
        }
        if ( _computeRootConstantUbo != 0 )
        {
            glDeleteBuffers( 1, &_computeRootConstantUbo );
            _computeRootConstantUbo = 0;
        }

        if ( _platformContext != nullptr )
        {
            _platformContext->destroy();
            _platformContext.reset();
        }
        _pHDC = nullptr;
        _pHRC = nullptr;

        _bInitialized = SW_FALSE;
        SW_LOG_INFO( "OpenGL RHI Device Shutdown cleanly." );
    }

    void OpenGLRHIDevice::resize( uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
        glViewport( 0, 0, static_cast<GLsizei>( width ), static_cast<GLsizei>( height ) );
        SW_LOG_TRACE( "OpenGL RHI Resized to %#×%#", width, height );
    }

    bool OpenGLRHIDevice::bindGraphicsContext()
    {
        if ( _bInitialized == SW_FALSE || _platformContext == nullptr )
            return false;

        // 플랫폼 구현은 조용하므로(경합은 정상이다) 실패 로그는 여기서 남긴다. 기다리지 않는
        // 호출부는 이 한 번의 시도로 끝낸다.
        if ( _platformContext->makeCurrent() == false )
        {
            SW_LOG_ERROR( "bindGraphicsContext failed - the context is held by another thread" );
            return false;
        }
        return true;
    }

    bool OpenGLRHIDevice::acquireGraphicsContextBlocking( uint32 timeoutMs )
    {
        if ( _bInitialized == SW_FALSE || _platformContext == nullptr )
            return false;

        // 경합이 없을 때의 정상 경로 - 대개 여기서 끝난다.
        if ( _platformContext->makeCurrent() )
            return true;

        // 렌더 워커가 프레임 끝마다 놓는다. 조용히 다시 집는다 - 시도마다 로그를 남기면 정상
        // 경합이 오류로 보인다(실제로 -gl -EnableEditor 의 ERROR_BUSY 2건이 그것이었다).
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
        while ( std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            if ( _platformContext->makeCurrent() )
                return true;
        }

        SW_LOG_ERROR( "acquireGraphicsContextBlocking timed out - GL calls on this thread will be dropped (ms=%#)",
                      timeoutMs );
        return false;
    }

    bool OpenGLRHIDevice::isGraphicsContextCurrent() const
    {
        if ( _bInitialized == SW_FALSE || _platformContext == nullptr )
            return false;
        return _platformContext->isCurrent();
    }

    void OpenGLRHIDevice::unbindGraphicsContext()
    {
        if ( _bInitialized == SW_FALSE || _platformContext == nullptr )
            return;
        _platformContext->clearCurrent();
    }
} // namespace sw
