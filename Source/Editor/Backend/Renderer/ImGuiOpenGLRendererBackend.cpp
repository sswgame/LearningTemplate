/**
 * @file ImGuiOpenGLRendererBackend.cpp
 * @brief Auto-generated documentation header
 */
#include "ImGuiOpenGLRendererBackend.h"
#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include "Core/Graphics/RHI/IRHIDevice.h"

#include "Core/Common/CommonMacros.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Common/PlatformHeaders.h"

struct WGL_WindowData
{
	HDC hDC;
};
static HGLRC g_MainWindowRC = nullptr;

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
	{
		wglMakeCurrent( data->hDC, g_MainWindowRC );
	}
}

static void Hook_Renderer_SwapBuffers( ImGuiViewport* viewport, void* )
{
	if ( WGL_WindowData* data = static_cast<WGL_WindowData*>( viewport->RendererUserData ) )
		::SwapBuffers( data->hDC );
}
#endif

namespace sw
{
	bool ImGuiOpenGLRendererBackend::initialize( class IRHIDevice* rhiDevice )
	{
		bool bResult = ImGui_ImplOpenGL3_Init( "#version 460" );

#if defined( SW_PLATFORM_WINDOWS )
		ImGuiIO& io = ImGui::GetIO();
		if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
		{
			g_MainWindowRC					   = static_cast<HGLRC>( rhiDevice->getNativeContext() );
			ImGuiPlatformIO& platform_io	   = ImGui::GetPlatformIO();
			platform_io.Renderer_CreateWindow  = Hook_Renderer_CreateWindow;
			platform_io.Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
			platform_io.Renderer_SwapBuffers   = Hook_Renderer_SwapBuffers;
			platform_io.Platform_RenderWindow  = Hook_Platform_RenderWindow;
		}
#endif
		return bResult;
	}

	void ImGuiOpenGLRendererBackend::shutdown()
	{
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplOpenGL3_Shutdown();
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
}
