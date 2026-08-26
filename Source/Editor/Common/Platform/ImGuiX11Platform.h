/**
 * @file ImGuiX11Platform.h
 * @brief ImGui X11 플랫폼 백엔드 (멀티 뷰포트 포함)
 */
#pragma once

#if defined( SW_PLATFORM_LINUX )

	#include "Core/Common/PlatformOsHeaders.h"

	// X11 매크로가 ImGui 식별자와 충돌하므로 ImGui/X11 경계에서만 undef 합니다.
	#ifdef None
		#undef None
	#endif
	#ifdef Success
		#undef Success
	#endif
	#ifdef Status
		#undef Status
	#endif
	#ifdef Always
		#undef Always
	#endif
	#ifdef Complex
		#undef Complex
	#endif

// ------------------------------------------------------------------------------
// 1) ImGui_ImplX11 — 플랫폼 백엔드 (Init / Shutdown / NewFrame / Event)
//    X11 매크로는 위에서 undef
// ------------------------------------------------------------------------------
/** @brief ImGui X11 플랫폼 백엔드를 초기화합니다. */
bool ImGui_ImplX11_Init( Display* pDisplay, Window window );
/** @brief ImGui X11 플랫폼 백엔드를 종료합니다. */
void ImGui_ImplX11_Shutdown();
/** @brief ImGui X11 프레임을 시작합니다. */
void ImGui_ImplX11_NewFrame();
/** @brief XEvent를 ImGui로 전달합니다. */
bool ImGui_ImplX11_ProcessEvent( XEvent* pEvent );

#endif // SW_PLATFORM_LINUX
