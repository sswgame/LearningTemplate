#pragma once
/**
 * @file ImGuiX11Platform.h
 * @brief ImGui X11 플랫폼 백엔드 (멀티 뷰포트 포함)
 */

#if defined( SW_PLATFORM_LINUX )

	#include <X11/Xlib.h>

bool ImGui_ImplX11_Init( Display* display, Window window );
void ImGui_ImplX11_Shutdown();
void ImGui_ImplX11_NewFrame();
bool ImGui_ImplX11_ProcessEvent( XEvent* event );

#endif // SW_PLATFORM_LINUX
