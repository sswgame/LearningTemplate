#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiOpenGLRendererBackend.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#if defined( SW_PLATFORM_WINDOWS )

struct WGL_WindowData
{
	HDC _hDC;
};

static HGLRC s_MainWindowRC{ nullptr };
#elif defined( SW_PLATFORM_LINUX )
	// vcpkg Khronos glxext.h가 X11 Status를 참조해 일부 include 순서에서 깨집니다.
	#define GLX_GLXEXT_LEGACY
	#include <GL/glx.h>
	#include "Core/Common/X11MacroUndef.h"

struct GLX_WindowData
{
	Display*   _pDisplay{ nullptr };
	GLXContext _ctx{ nullptr };
	Window	   _win{ 0 };
};
static Display*	  s_MainDisplay{ nullptr };
static GLXContext s_MainContext{ nullptr };
#endif

#if defined( SW_PLATFORM_WINDOWS )

static bool CreateDeviceWGL( HWND hWnd, WGL_WindowData* pData )
{
	HDC					  hDc = GetDC( hWnd );
	PIXELFORMATDESCRIPTOR pfd{};
	pfd.nSize		 = sizeof( pfd );
	pfd.nVersion	 = 1;
	pfd.dwFlags		 = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType	 = PFD_TYPE_RGBA;
	pfd.cColorBits	 = 32;
	pfd.cDepthBits	 = 24;
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

static void CleanupDeviceWGL( HWND hWnd, WGL_WindowData* pData )
{
	wglMakeCurrent( nullptr, nullptr );
	ReleaseDC( hWnd, pData->_hDC );
}

static void Hook_Renderer_CreateWindow( ImGuiViewport* pViewport )
{
	WGL_WindowData* pData = sw_new WGL_WindowData();
	CreateDeviceWGL( static_cast<HWND>( pViewport->PlatformHandle ), pData );
	pViewport->RendererUserData = pData;
}

static void Hook_Renderer_DestroyWindow( ImGuiViewport* pViewport )
{
	if ( pViewport->RendererUserData != nullptr )
	{
		WGL_WindowData* pData = static_cast<WGL_WindowData*>( pViewport->RendererUserData );
		CleanupDeviceWGL( static_cast<HWND>( pViewport->PlatformHandle ), pData );
		sw_delete( pData );
		pViewport->RendererUserData = nullptr;
	}
}

static void Hook_Platform_RenderWindow( ImGuiViewport* pViewport, void* )
{
	WGL_WindowData* pData = static_cast<WGL_WindowData*>( pViewport->RendererUserData );
	if ( pData != nullptr )
		wglMakeCurrent( pData->_hDC, s_MainWindowRC );
}

static void Hook_Renderer_SwapBuffers( ImGuiViewport* pViewport, void* )
{
	WGL_WindowData* pData = static_cast<WGL_WindowData*>( pViewport->RendererUserData );
	if ( pData != nullptr )
	{
		SwapBuffers( pData->_hDC );
		wglMakeCurrent( nullptr, nullptr );
	}
}
#elif defined( SW_PLATFORM_LINUX )
static void Hook_Renderer_CreateWindow_GLX( ImGuiViewport* pViewport )
{
	if ( s_MainDisplay == nullptr || s_MainContext == nullptr || pViewport == nullptr )
		return;
	GLX_WindowData* pData = sw_new GLX_WindowData();
	pData->_pDisplay	  = s_MainDisplay;
	pData->_win			  = static_cast<Window>( reinterpret_cast<uintptr_t>( pViewport->PlatformHandleRaw ) );

	XWindowAttributes attrs{};
	XVisualInfo*	  pVi{ nullptr };
	if ( pData->_win != 0 && XGetWindowAttributes( pData->_pDisplay, pData->_win, &attrs ) != 0 && attrs.visual != nullptr )
	{
		XVisualInfo templateInfo{};
		templateInfo.visualid = XVisualIDFromVisual( attrs.visual );
		int32 count{ 0 };
		pVi = XGetVisualInfo( pData->_pDisplay, VisualIDMask, &templateInfo, &count );
	}
	if ( pVi != nullptr )
	{
		pData->_ctx = glXCreateContext( pData->_pDisplay, pVi, s_MainContext, 1 );
		XFree( pVi );
	}
	pViewport->RendererUserData = pData;
}

static void Hook_Renderer_DestroyWindow_GLX( ImGuiViewport* pViewport )
{
	if ( pViewport == nullptr || pViewport->RendererUserData == nullptr )
		return;
	GLX_WindowData* pData = static_cast<GLX_WindowData*>( pViewport->RendererUserData );
	if ( pData->_ctx != nullptr )
		glXDestroyContext( pData->_pDisplay, pData->_ctx );
	sw_delete( pData );
	pViewport->RendererUserData = nullptr;
}

static void Hook_Platform_RenderWindow_GLX( ImGuiViewport* pViewport, void* )
{
	GLX_WindowData* pData = static_cast<GLX_WindowData*>( pViewport->RendererUserData );
	if ( pData != nullptr )
		glXMakeCurrent( pData->_pDisplay, pData->_win, pData->_ctx ? pData->_ctx : s_MainContext );
}

static void Hook_Renderer_SwapBuffers_GLX( ImGuiViewport* pViewport, void* )
{
	GLX_WindowData* pData = static_cast<GLX_WindowData*>( pViewport->RendererUserData );
	if ( pData != nullptr )
	{
		glXSwapBuffers( pData->_pDisplay, pData->_win );
		glXMakeCurrent( pData->_pDisplay, 0, nullptr );
	}
}
#endif

namespace sw::editor
{
	SW_LOG_CALLER( "ImGuiOpenGL" );

	bool ImGuiOpenGLRendererBackend::initialize( class IRHIDevice* pRhiDevice )
	{
		_pRHIDevice = ( pRhiDevice != nullptr && pRhiDevice->getBackendType() == RHIBackend::OpenGL ) ? pRhiDevice : nullptr;

		if ( _pRHIDevice != nullptr )
			_pRHIDevice->bindGraphicsContext();
		const bool bResult = ImGui_ImplOpenGL3_Init( "#version 460" );
		if ( _pRHIDevice != nullptr )
			_pRHIDevice->unbindGraphicsContext();

#if defined( SW_PLATFORM_WINDOWS )
		ImGuiIO& io = ImGui::GetIO();
		if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
		{
			s_MainWindowRC					   = static_cast<HGLRC>( pRhiDevice->getNativeContext() );
			ImGuiPlatformIO& platform_io	   = ImGui::GetPlatformIO();
			platform_io.Renderer_CreateWindow  = Hook_Renderer_CreateWindow;
			platform_io.Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
			platform_io.Renderer_SwapBuffers   = Hook_Renderer_SwapBuffers;
			platform_io.Platform_RenderWindow  = Hook_Platform_RenderWindow;
		}
#elif defined( SW_PLATFORM_LINUX )
		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) && pRhiDevice != nullptr )
		{
			s_MainDisplay = static_cast<Display*>( pRhiDevice->getNativeDevice() );
			s_MainContext = static_cast<GLXContext>( pRhiDevice->getNativeContext() );
			if ( s_MainDisplay == nullptr )
				s_MainDisplay = glXGetCurrentDisplay();
			ImGuiPlatformIO& platform_io	   = ImGui::GetPlatformIO();
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
		if ( _pRHIDevice != nullptr )
			_pRHIDevice->bindGraphicsContext();
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplOpenGL3_Shutdown();
		if ( _pRHIDevice != nullptr )
			_pRHIDevice->unbindGraphicsContext();
		_pRHIDevice = nullptr;
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

	void ImGuiOpenGLRendererBackend::render( class IRHIDevice* pRhiDevice, ImDrawData* pDrawData )
	{
		(void)pRhiDevice;
		if ( pDrawData != nullptr && _pRHIDevice != nullptr )
			ImGui_ImplOpenGL3_RenderDrawData( pDrawData );
	}

	void* ImGuiOpenGLRendererBackend::registerTexture( RHITextureHandle texture )
	{
		if ( texture == 0 || _pRHIDevice == nullptr )
			return nullptr;

		const uint32 glName = _pRHIDevice->getNativeTextureName( texture );
		if ( glName == 0 )
		{
			SW_LOG_ERROR( "Failed to resolve GL texture for RHI handle %#", texture );
			return nullptr;
		}

		// imgui_impl_opengl3는 ImTextureID를 GLuint로 취급합니다.
		return reinterpret_cast<void*>( static_cast<uintptr_t>( glName ) );
	}

	void ImGuiOpenGLRendererBackend::unregisterTexture( void* pTextureID )
	{
		// GL 텍스처 이름은 RHI가 소유하고, ImGui는 ID만 저장합니다.
		(void)pTextureID;
	}
} // namespace sw::editor
