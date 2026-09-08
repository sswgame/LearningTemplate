#include "pch.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandList.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    namespace
    {
        /**
         * @brief GL 스왑 인터벌(vsync) 을 플랫폼 API 로 넘깁니다.
         * @details 예전엔 공유 헤더(OpenGLRHIDeviceInternal.h)에 있었다. 호출자는 여기 하나뿐인데
         *          헤더에 두면 함수가 외부 링키지를 갖고, 그 안의 proc 주소 static 이 이 헤더를 컴파일한
         *          바이너리(Engine.dll / RHI_GL.dll)마다 따로 생긴다(-Wunique-object-duplication).
         *          vsync 가 **바뀔 때만** 부르므로 주소를 캐시해서 얻는 것도 사실상 없다.
         */
        struct OpenGLRHIDeviceSubmissionInternal
        {
            static void applyVsyncInterval( void* pHdc, void* pHrc, bool vsync )
            {
                (void)pHdc;
#if defined( SW_PLATFORM_WINDOWS )
                (void)pHrc;
                using PFNWGLSWAPINTERVALEXTPROC                       = BOOL( WINAPI* )( int32 );
                static PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>( wglGetProcAddress( "wglSwapIntervalEXT" ) );
                if ( s_wglSwapIntervalEXT != nullptr )
                    s_wglSwapIntervalEXT( vsync ? 1 : 0 );
#elif defined( SW_PLATFORM_LINUX )
                (void)pHrc;
                using PFNGLXSWAPINTERVALEXTPROC = void ( * )( Display*, GLXDrawable, int32 );
                static PFNGLXSWAPINTERVALEXTPROC s_glXSwapIntervalEXT =
                    reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>( glXGetProcAddressARB( (const GLubyte*)"glXSwapIntervalEXT" ) );
                if ( s_glXSwapIntervalEXT != nullptr && pHdc != nullptr )
                {
                    Display* pDpy = static_cast<Display*>( pHdc );
                    s_glXSwapIntervalEXT( pDpy, glXGetCurrentDrawable(), vsync ? 1 : 0 );
                }
#elif defined( SW_PLATFORM_MACOS )
                (void)pHdc;
                if ( pHrc != nullptr )
                {
                    id              context                               = static_cast<id>( pHrc );
                    GLint           interval                              = vsync ? 1 : 0;
                    constexpr GLint kNsOpenGlContextParameterSwapInterval = 222;
                    ( (void ( * )( id, SEL, GLint*, GLint ))objc_msgSend )(
                        context, sel_registerName( "setValues:forParameter:" ), &interval, kNsOpenGlContextParameterSwapInterval );
                }
#endif
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    void OpenGLRHIDevice::beginFrame( const float4& clearColor )
    {
        if ( _bInitialized == SW_FALSE )
            return;

#if defined( SW_PLATFORM_WINDOWS )
        if ( _pHDC != nullptr && _pHRC != nullptr )
            wglMakeCurrent( static_cast<HDC>( _pHDC ), static_cast<HGLRC>( _pHRC ) );
#endif

        // 백버퍼(FBO 0) 바인딩과 클리어는 여기서 하지 않는다 — beginFrame 은 프레임 수명주기(GL 은
        // 컨텍스트 확보)만 담당하고, 백버퍼 타깃팅은 beginRenderPass(핸들 0) 가 명시적으로 한다
        // (docs/05_RHI_FrameContract.md S2). 뷰포트는 기본 상태로 남겨둔다.
        (void)clearColor;
        glViewport( 0, 0, static_cast<GLsizei>( _width ), static_cast<GLsizei>( _height ) );
    }

    void OpenGLRHIDevice::endFrame( bool vsync, bool bPresent )
    {
        if ( _bInitialized == SW_FALSE )
            return;

        if ( bPresent == false )
        {
            _releaseQueue.tickFrame();
            return;
        }

        const int8 desired = vsync ? 1 : 0;
        if ( _lastVsync != desired )
        {
            OpenGLRHIDeviceSubmissionInternal::applyVsyncInterval( _pHDC, _pHRC, vsync );
            _lastVsync = desired;
        }

#if defined( SW_PLATFORM_WINDOWS )
        // Ensure the RHI context is current after ImGui multi-viewport may have switched DCs.
        if ( _pHDC && _pHRC )
            wglMakeCurrent( static_cast<HDC>( _pHDC ), static_cast<HGLRC>( _pHRC ) );
        SwapBuffers( static_cast<HDC>( _pHDC ) );
#elif defined( SW_PLATFORM_LINUX )
        glXSwapBuffers( (Display*)_pHDC, (Window)(uintptr_t)_pHWnd );
#elif defined( SW_PLATFORM_MACOS )
        id context = (id)_pHRC;
        ( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "flushBuffer" ) );
#endif
        _releaseQueue.tickFrame();
    }

    void OpenGLRHIDevice::waitIdle()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        ScopedOpenGLContext ctxScope( this );
        glFinish();
        _releaseQueue.flushAll();
    }

    unique_ptr<IRHICommandList> OpenGLRHIDevice::createCommandList()
    {
        return make_unique<OpenGLRHICommandList>( this );
    }

    void OpenGLRHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        // 기록이 이미 GL 호출로 즉시 나갔으므로(beginCommandList에서 컨텍스트 재바인딩까지 마침)
        // 순서를 맞출 일은 없다. 다만 GL 도 커맨드를 모아뒀다가 보내므로, 즉시 모드에서는
        // 리스트 경계마다 밀어내 오류가 어느 리스트에서 났는지 드러나게 한다.
        (void)pCmdList;
        if ( _bImmediateSubmit )
            glFlush();
    }
} // namespace sw
