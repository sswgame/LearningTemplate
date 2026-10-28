#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiOpenGLRendererBackend.h"

#include "Core/Common/PlatformOSHeaders.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#include "Engine/Graphics/RHI/IRHIContextThreadBinding.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#if defined( SW_PLATFORM_WINDOWS )

struct WGL_WindowData
{
    HWND _hWnd; ///< 만들 때 읽은 창입니다. 해제는 이 값으로 합니다(뷰포트 핸들을 다시 읽지 않는다)
    HDC  _hDC;
};

static HGLRC s_MainWindowRC{ nullptr };
#elif defined( SW_PLATFORM_LINUX )
    // vcpkg 의 Khronos glxext.h 가 X11 Status 를 참조해, include 순서에 따라 깨진다.
    #define GLX_GLXEXT_LEGACY
    #include <GL/glx.h>
    #include "Core/Common/X11MacroUndef.h"

    #include "Engine/Window/IWindow.h"

struct GLX_WindowData
{
    Display*   _pDisplay{ nullptr };
    GLXContext _context{ nullptr };
    Window     _win{ 0 };
};
static Display*   s_MainDisplay{ nullptr };
static GLXContext s_MainContext{ nullptr };
#endif

#if defined( SW_PLATFORM_WINDOWS )

namespace
{
    bool CreateDeviceWGL( HWND hWnd, WGL_WindowData* pData )
    {
        HDC                   hDc = GetDC( hWnd );
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize        = sizeof( pfd );
        pfd.nVersion     = 1;
        pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType   = PFD_TYPE_RGBA;
        pfd.cColorBits   = 32;
        pfd.cDepthBits   = 24;
        pfd.cStencilBits = 8;

        int32 pf = ChoosePixelFormat( hDc, &pfd );
        if ( pf == 0 )
            return false;
        if ( SetPixelFormat( hDc, pf, &pfd ) == FALSE )
            return false;
        ReleaseDC( hWnd, hDc );

        pData->_hDC = GetDC( hWnd );
        return true;
    }

    void CleanupDeviceWGL( HWND hWnd, WGL_WindowData* pData )
    {
        wglMakeCurrent( nullptr, nullptr );
        ReleaseDC( hWnd, pData->_hDC );
    }

    void Hook_Renderer_CreateWindow( ImGuiViewport* pViewport )
    {
        // 뷰포트 핸들은 Win32 플랫폼 백엔드일 때만 HWND 다 — 아니면 오류를 남기고 이 뷰포트에는 GL 자원을 만들지 않는다(다른 핸들로 대신하지 않는다).
        HWND hWnd = static_cast<HWND>( sw::editor::IImGuiPlatformBackend::tryGetViewportWin32Handle( pViewport ) );
        if ( hWnd == nullptr )
        {
            SW_LOG_ERROR( "The ImGui OpenGL viewport window needs a Win32 viewport window with a handle" );
            return;
        }
        WGL_WindowData* pData = sw::make_unique<WGL_WindowData>().release(); // ImGui 뷰포트가 쥔다. `Hook_Renderer_DestroyWindow` 가 푼다
        pData->_hWnd          = hWnd;
        CreateDeviceWGL( hWnd, pData );
        pViewport->RendererUserData = pData;
    }

    void Hook_Renderer_DestroyWindow( ImGuiViewport* pViewport )
    {
        if ( pViewport->RendererUserData != nullptr )
        {
            WGL_WindowData* pData = static_cast<WGL_WindowData*>( pViewport->RendererUserData );
            CleanupDeviceWGL( pData->_hWnd, pData );
            sw_delete( pData );
            pViewport->RendererUserData = nullptr;
        }
    }

    void Hook_Platform_RenderWindow( ImGuiViewport* pViewport, void* )
    {
        WGL_WindowData* pData = static_cast<WGL_WindowData*>( pViewport->RendererUserData );
        if ( pData != nullptr )
            wglMakeCurrent( pData->_hDC, s_MainWindowRC );
    }

    void Hook_Renderer_SwapBuffers( ImGuiViewport* pViewport, void* )
    {
        WGL_WindowData* pData = static_cast<WGL_WindowData*>( pViewport->RendererUserData );
        if ( pData != nullptr )
        {
            SwapBuffers( pData->_hDC );
            wglMakeCurrent( nullptr, nullptr );
        }
    }
} // namespace
#elif defined( SW_PLATFORM_LINUX )
namespace
{
    void Hook_Renderer_CreateWindow_GLX( ImGuiViewport* pViewport )
    {
        const bool bCannotCreateViewportWindow = s_MainDisplay == nullptr || s_MainContext == nullptr || pViewport == nullptr;
        if ( bCannotCreateViewportWindow )
            return;
        // 뷰포트 핸들을 창 ID 로 읽는 것은 X11 플랫폼 백엔드일 때뿐이다 — 아니면 오류를 남기고 이 뷰포트에는 GL 자원을 만들지 않는다.
        sw::X11SurfaceHandles x11Surface;
        if ( sw::editor::IImGuiPlatformBackend::tryGetViewportX11Surface( pViewport, x11Surface ) == false )
        {
            SW_LOG_ERROR( "The ImGui OpenGL viewport window needs an X11 viewport window with a display connection" );
            return;
        }
        GLX_WindowData* pData = sw::make_unique<GLX_WindowData>().release(); // ImGui 뷰포트가 쥔다. 파괴 훅이 푼다
        pData->_pDisplay      = s_MainDisplay;
        pData->_win           = static_cast<Window>( x11Surface._window );

        XWindowAttributes attrs{};
        XVisualInfo*      pVi{ nullptr };
        const bool        bHasWindowVisual = pData->_win != 0 && XGetWindowAttributes( pData->_pDisplay, pData->_win, &attrs ) != 0 && attrs.visual != nullptr;
        if ( bHasWindowVisual )
        {
            XVisualInfo templateInfo{};
            templateInfo.visualid = XVisualIDFromVisual( attrs.visual );
            int32 count{ 0 };
            pVi = XGetVisualInfo( pData->_pDisplay, VisualIDMask, &templateInfo, &count );
        }
        if ( pVi != nullptr )
        {
            pData->_context = glXCreateContext( pData->_pDisplay, pVi, s_MainContext, 1 );
            XFree( pVi );
        }
        pViewport->RendererUserData = pData;
    }

    void Hook_Renderer_DestroyWindow_GLX( ImGuiViewport* pViewport )
    {
        if ( pViewport == nullptr || pViewport->RendererUserData == nullptr )
            return;
        GLX_WindowData* pData = static_cast<GLX_WindowData*>( pViewport->RendererUserData );
        if ( pData->_context != nullptr )
            glXDestroyContext( pData->_pDisplay, pData->_context );
        sw_delete( pData );
        pViewport->RendererUserData = nullptr;
    }

    void Hook_Platform_RenderWindow_GLX( ImGuiViewport* pViewport, void* )
    {
        GLX_WindowData* pData = static_cast<GLX_WindowData*>( pViewport->RendererUserData );
        if ( pData != nullptr )
            glXMakeCurrent( pData->_pDisplay, pData->_win, pData->_context ? pData->_context : s_MainContext );
    }

    void Hook_Renderer_SwapBuffers_GLX( ImGuiViewport* pViewport, void* )
    {
        GLX_WindowData* pData = static_cast<GLX_WindowData*>( pViewport->RendererUserData );
        if ( pData != nullptr )
        {
            glXSwapBuffers( pData->_pDisplay, pData->_win );
            glXMakeCurrent( pData->_pDisplay, 0, nullptr );
        }
    }
} // namespace
#endif

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiOpenGL" );

    bool ImGuiOpenGLRendererBackend::initialize( class IRHIDevice* pRHIDevice )
    {
        _pRHIDevice.set( ( pRHIDevice != nullptr && pRHIDevice->getBackendType() == RHIBackend::OpenGL ) ? pRHIDevice : nullptr );

        // GL 디바이스는 늘 컨텍스트 스레드 바인딩 능력을 가진다. 디바이스가 없으면 붙일 것도 없다.
        IRHIContextThreadBinding* pContextBinding = _pRHIDevice.get() != nullptr ? _pRHIDevice->findContextThreadBinding() : nullptr;
        if ( pContextBinding != nullptr )
            (void)pContextBinding->bindGraphicsContext(); // 실패 로그는 디바이스가 남긴다
        const bool bResult = ImGui_ImplOpenGL3_Init( "#version 460" );
        if ( pContextBinding != nullptr )
            pContextBinding->unbindGraphicsContext();

#if defined( SW_PLATFORM_WINDOWS )
        ImGuiIO& io = ImGui::GetIO();
        // **검증을 거친 `_pRHIDevice.get()` 를 쓴다.** 매개변수 `pRHIDevice` 를 그대로 역참조하면 `initialize( nullptr )` 에서
        // 널 역참조다 — 바로 위에서 널 · OpenGL 이 아닌 디바이스를 걸러 멤버를 nullptr 로 만들어 둔다. 멤버를 쓰면
        // "OpenGL 디바이스일 때만 GL 뷰포트 훅을 건다" 도 함께 맞는다(아래 Linux 분기와 같다).
        if ( ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) && _pRHIDevice.get() != nullptr )
        {
            // HGLRC 는 렌더 스레드가 보조 뷰포트를 그린 뒤 메인 창으로 되돌아갈 때(wglMakeCurrent) 쓰는 이름이다. 그 훅은 렌더 스레드에서만 돈다.
            // 종류 확인은 `withNativeContext` 가 하고, 값 확인은 지역 변수에서 한 뒤 정적 칸에 한 번만 쓴다.
            HGLRC      hMainRC{ nullptr };
            const bool bContextVisited = _pRHIDevice->withNativeContext( RHIBackend::OpenGL, SW_DELEGATE_LAMBDA( RHINativeContextVisitor, [&hMainRC]( void* pContext )
            { hMainRC = static_cast<HGLRC>( pContext ); } ) );
            if ( bContextVisited == false || hMainRC == nullptr )
            {
                SW_LOG_ERROR( "The OpenGL device has no WGL render context, so ImGui viewports cannot be enabled" );
                return false;
            }
            s_MainWindowRC                     = hMainRC;
            ImGuiPlatformIO& platform_io       = ImGui::GetPlatformIO();
            platform_io.Renderer_CreateWindow  = Hook_Renderer_CreateWindow;
            platform_io.Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
            platform_io.Renderer_SwapBuffers   = Hook_Renderer_SwapBuffers;
            platform_io.Platform_RenderWindow  = Hook_Platform_RenderWindow;
        }
#elif defined( SW_PLATFORM_LINUX )
        ImGuiIO& io = ImGui::GetIO();
        if ( ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) && _pRHIDevice.get() != nullptr )
        {
            // GLX 디바이스의 디바이스 칸은 컨텍스트를 만든 Display* 다(GLXPlatformContext). 값이 없으면 현재 문맥의 연결로 대신하지 않고 오류로 멈춘다.
            // 종류 확인과 값 확인은 `queryNativeHandles` 한 곳에서 한다(디바이스 칸이 비면 false).
            // 검사는 지역 변수에서 하고 정적 칸에는 값을 한 번만 쓴다 — 검사 뒤 채우기(`lazyFill`) 모양이 아니다.
            RHINativeHandles glNative{};
            if ( _pRHIDevice->queryNativeHandles( RHIBackend::OpenGL, glNative ) == false )
            {
                SW_LOG_ERROR( "The OpenGL device has no GLX display connection, so ImGui viewports cannot be enabled" );
                return false;
            }
            GLXContext mainContext{ nullptr };
            const bool bContextVisited = _pRHIDevice->withNativeContext( RHIBackend::OpenGL, SW_DELEGATE_LAMBDA( RHINativeContextVisitor, [&mainContext]( void* pContext )
            { mainContext = static_cast<GLXContext>( pContext ); } ) );
            if ( bContextVisited == false )
                return false;
            s_MainDisplay                      = static_cast<Display*>( glNative._pDevice );
            s_MainContext                      = mainContext;
            ImGuiPlatformIO& platform_io       = ImGui::GetPlatformIO();
            platform_io.Renderer_CreateWindow  = Hook_Renderer_CreateWindow_GLX;
            platform_io.Renderer_DestroyWindow = Hook_Renderer_DestroyWindow_GLX;
            platform_io.Renderer_SwapBuffers   = Hook_Renderer_SwapBuffers_GLX;
            platform_io.Platform_RenderWindow  = Hook_Platform_RenderWindow_GLX;
        }
#endif
        return bResult;
    }

    void ImGuiOpenGLRendererBackend::shutdown()
    {
        // 이 백엔드는 미룰 네이티브 자원이 없지만, 에디터가 같은 큐에 맡긴 해제(게임 뷰 렌더 타깃)가 이 모듈의 코드를 가리킨다.
        flushDrawReleases( _pRHIDevice.get() );
        IRHIContextThreadBinding* pContextBinding = _pRHIDevice.get() != nullptr ? _pRHIDevice->findContextThreadBinding() : nullptr;
        if ( pContextBinding != nullptr )
            (void)pContextBinding->bindGraphicsContext(); // 실패 로그는 디바이스가 남긴다
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
            ImGui_ImplOpenGL3_Shutdown();
        if ( pContextBinding != nullptr )
            pContextBinding->unbindGraphicsContext();
        _pRHIDevice.reset();
    }

    void ImGuiOpenGLRendererBackend::newFrame()
    {
        // requiresRenderThreadContext()==true 이므로 ImGuiEditor 가 present 훅(렌더 스레드)에서
        // 호출한다. GL 컨텍스트는 이미 렌더 스레드에 바인딩돼 있으므로 여기서 잡지 않는다.
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
            ImGui_ImplOpenGL3_NewFrame();
    }

    void ImGuiOpenGLRendererBackend::processTextureUpdates()
    {
        updatePendingTextures( &ImGui_ImplOpenGL3_UpdateTexture );
    }

    void ImGuiOpenGLRendererBackend::render( class IRHIDevice* pRHIDevice, ImDrawData* pDrawData )
    {
        (void)pRHIDevice;
        if ( pDrawData != nullptr && _pRHIDevice.get() != nullptr )
            ImGui_ImplOpenGL3_RenderDrawData( pDrawData );
    }

    void* ImGuiOpenGLRendererBackend::registerTexture( RHITextureHandle texture )
    {
        if ( texture == 0 || _pRHIDevice.get() == nullptr )
            return nullptr;

        // imgui_impl_opengl3 는 ImTextureID 를 GLuint 로 취급한다. GL 디바이스의 네이티브 텍스처 포인터가 그 이름을 포인터 폭으로 넓힌 값이다.
        void* pTextureID = _pRHIDevice->tryGetNativeTexturePointer( RHIBackend::OpenGL, texture );
        if ( pTextureID == nullptr )
        {
            SW_LOG_ERROR( "Failed to resolve GL texture for RHI handle %#", texture );
            return nullptr;
        }
        return pTextureID;
    }

    void ImGuiOpenGLRendererBackend::unregisterTexture( void* pTextureID )
    {
        // GL 텍스처 이름은 RHI 가 소유하고, ImGui 는 ID 만 저장한다.
        (void)pTextureID;
    }
} // namespace sw::editor
