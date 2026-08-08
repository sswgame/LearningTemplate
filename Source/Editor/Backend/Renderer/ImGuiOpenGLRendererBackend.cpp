/**
 * @file ImGuiOpenGLRendererBackend.cpp
 * @brief ImGui OpenGL 렌더러 구현
 */
#include "ImGuiOpenGLRendererBackend.h"
#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Utility/Log/Logger.h"

#if defined( SW_PLATFORM_WINDOWS )

struct WGL_WindowData
{
	HDC hDC;
};
static HGLRC s_MainWindowRC = nullptr;
#elif defined( SW_PLATFORM_LINUX )
	#include <X11/Xlib.h>
	#include <X11/Xutil.h>
	#include <GL/glx.h>
	#ifdef None
		#undef None
	#endif

struct GLX_WindowData
{
	Display*   dpy = nullptr;
	GLXContext ctx = nullptr;
	Window	   win = 0;
};
static Display*	  s_MainDisplay = nullptr;
static GLXContext s_MainContext = nullptr;
#endif

#if defined( SW_PLATFORM_WINDOWS )

static bool CreateDeviceWGL( HWND hWnd, WGL_WindowData* data )
{
	HDC					  hDc = ::GetDC( hWnd );
	PIXELFORMATDESCRIPTOR pfd{};
	pfd.nSize		 = sizeof( pfd );
	pfd.nVersion	 = 1;
	pfd.dwFlags		 = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType	 = PFD_TYPE_RGBA;
	pfd.cColorBits	 = 32;
	pfd.cDepthBits	 = 24;
	pfd.cStencilBits = 8;

	int pf = ::ChoosePixelFormat( hDc, &pfd );
	if ( pf == 0 )
		return false;
	if ( ::SetPixelFormat( hDc, pf, &pfd ) == FALSE )
		return false;
	::ReleaseDC( hWnd, hDc );

	data->hDC = ::GetDC( hWnd );
	return true;
}

static void CleanupDeviceWGL( HWND hWnd, WGL_WindowData* data )
{
	wglMakeCurrent( nullptr, nullptr );
	::ReleaseDC( hWnd, data->hDC );
}

static void Hook_Renderer_CreateWindow( ImGuiViewport* viewport )
{
	WGL_WindowData* data = new WGL_WindowData;
	CreateDeviceWGL( static_cast<HWND>( viewport->PlatformHandle ), data );
	viewport->RendererUserData = data;
}

static void Hook_Renderer_DestroyWindow( ImGuiViewport* viewport )
{
	if ( viewport->RendererUserData != nullptr )
	{
		WGL_WindowData* data = static_cast<WGL_WindowData*>( viewport->RendererUserData );
		CleanupDeviceWGL( static_cast<HWND>( viewport->PlatformHandle ), data );
		delete data;
		viewport->RendererUserData = nullptr;
	}
}

static void Hook_Platform_RenderWindow( ImGuiViewport* viewport, void* )
{
	if ( WGL_WindowData* data = static_cast<WGL_WindowData*>( viewport->RendererUserData ) )
		wglMakeCurrent( data->hDC, s_MainWindowRC );
}

static void Hook_Renderer_SwapBuffers( ImGuiViewport* viewport, void* )
{
	if ( WGL_WindowData* data = static_cast<WGL_WindowData*>( viewport->RendererUserData ) )
		::SwapBuffers( data->hDC );
}
#elif defined( SW_PLATFORM_LINUX )
static void Hook_Renderer_CreateWindow_GLX( ImGuiViewport* viewport )
{
	if ( s_MainDisplay == nullptr || s_MainContext == nullptr || viewport == nullptr )
		return;
	auto* data = new GLX_WindowData();
	data->dpy  = s_MainDisplay;
	data->win  = static_cast<Window>( reinterpret_cast<uintptr_t>( viewport->PlatformHandleRaw ) );

	XWindowAttributes attrs{};
	XVisualInfo*	  vi = nullptr;
	if ( data->win != 0 && XGetWindowAttributes( data->dpy, data->win, &attrs ) != 0 && attrs.visual != nullptr )
	{
		XVisualInfo templateInfo{};
		templateInfo.visualid = XVisualIDFromVisual( attrs.visual );
		int count			  = 0;
		vi					  = XGetVisualInfo( data->dpy, VisualIDMask, &templateInfo, &count );
	}
	if ( vi != nullptr )
	{
		data->ctx = glXCreateContext( data->dpy, vi, s_MainContext, True );
		XFree( vi );
	}
	viewport->RendererUserData = data;
}

static void Hook_Renderer_DestroyWindow_GLX( ImGuiViewport* viewport )
{
	if ( viewport == nullptr || viewport->RendererUserData == nullptr )
		return;
	auto* data = static_cast<GLX_WindowData*>( viewport->RendererUserData );
	if ( data->ctx )
		glXDestroyContext( data->dpy, data->ctx );
	delete data;
	viewport->RendererUserData = nullptr;
}

static void Hook_Platform_RenderWindow_GLX( ImGuiViewport* viewport, void* )
{
	if ( auto* data = static_cast<GLX_WindowData*>( viewport->RendererUserData ) )
		glXMakeCurrent( data->dpy, data->win, data->ctx ? data->ctx : s_MainContext );
}

static void Hook_Renderer_SwapBuffers_GLX( ImGuiViewport* viewport, void* )
{
	if ( auto* data = static_cast<GLX_WindowData*>( viewport->RendererUserData ) )
		glXSwapBuffers( data->dpy, data->win );
}
#endif

namespace sw
{
	bool ImGuiOpenGLRendererBackend::initialize( class IRHIDevice* rhiDevice )
	{
		_rhiDevice = ( rhiDevice != nullptr && rhiDevice->getBackendType() == RHIBackend::OpenGL ) ? rhiDevice : nullptr;

		bool bResult = ImGui_ImplOpenGL3_Init( "#version 460" );

#if defined( SW_PLATFORM_WINDOWS )
		ImGuiIO& io = ImGui::GetIO();
		if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
		{
			s_MainWindowRC					   = static_cast<HGLRC>( rhiDevice->getNativeContext() );
			ImGuiPlatformIO& platform_io	   = ImGui::GetPlatformIO();
			platform_io.Renderer_CreateWindow  = Hook_Renderer_CreateWindow;
			platform_io.Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
			platform_io.Renderer_SwapBuffers   = Hook_Renderer_SwapBuffers;
			platform_io.Platform_RenderWindow  = Hook_Platform_RenderWindow;
		}
#elif defined( SW_PLATFORM_LINUX )
		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) && rhiDevice != nullptr )
		{
			s_MainDisplay					   = static_cast<Display*>( rhiDevice->getNativeDevice() );
			s_MainContext					   = static_cast<GLXContext>( rhiDevice->getNativeContext() );
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
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplOpenGL3_Shutdown();
		_rhiDevice = nullptr;
	}

	void* ImGuiOpenGLRendererBackend::registerTexture( RHITextureHandle texture )
	{
		if ( texture == 0 || _rhiDevice == nullptr )
			return nullptr;

		const uint32 glName = _rhiDevice->getNativeTextureName( texture );
		if ( glName == 0 )
		{
			SW_LOG_ERROR( "[ImGuiOpenGL] Failed to resolve GL texture for RHI handle %#", texture );
			return nullptr;
		}

		// imgui_impl_opengl3 treats ImTextureID as GLuint.
		return reinterpret_cast<void*>( static_cast<uintptr_t>( glName ) );
	}

	void ImGuiOpenGLRendererBackend::unregisterTexture( void* textureID )
	{
		// GL texture name is owned by RHI; ImGui only stores the id.
		(void)textureID;
	}

	void ImGuiOpenGLRendererBackend::newFrame()
	{
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplOpenGL3_NewFrame();
	}

	void ImGuiOpenGLRendererBackend::render( class IRHIDevice* rhiDevice )
	{
		(void)rhiDevice;
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );
	}
} // namespace sw
