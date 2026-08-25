/**
 * @file NativeWindowEvent.h
 * @brief 플랫폼 메시지 페이로드 (Win32 / X11 등이 동일 슬롯을 채움)
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
	/// @brief OS 창 이벤트 (리사이즈, 닫기, 포커스 등)
	struct NativeWindowEvent
	{
		/** @brief X11: processMessages가 XEvent* 를 lParam에 실을 때 사용 */
		static constexpr uint32 kMessageX11 = 0x8001;

		/** @brief 네이티브 창 핸들 (Win32: HWND, macOS: NSWindow, X11: Window) */
		void* _pNativeWindow{ nullptr };
		/** @brief 플랫폼 메시지 코드 (Win32: UINT msg, 기타 OS는 유사 목적에 맞게 매핑) */
		uint32 _message{ 0 };
		/** @brief 첫 번째 파라미터 (Win32: WPARAM) */
		uintptr_t _wParam{ 0 };
		/** @brief 두 번째 파라미터 (Win32: LPARAM) */
		intptr_t _lParam{ 0 };
	};
} // namespace sw
