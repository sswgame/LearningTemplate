#pragma once

/**
 * @file NativeWindowEvent.h
 * @brief 플랫폼 메시지 페이로드 (Win32 / X11 등이 동일 슬롯을 채움)
 */

#include "Core/Common/Types.h"

namespace sw
{
	struct NativeWindowEvent
	{
		/** @brief X11: processMessages가 XEvent* 를 lParam에 실을 때 사용 */
		static constexpr uint32 kMessageX11 = 0x8001;

		void*	  nativeWindow = nullptr;
		uint32	  message	   = 0;
		uintptr_t wParam	   = 0;
		intptr_t  lParam	   = 0;
	};
} // namespace sw
