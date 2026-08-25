/**
 * @file InputKeyMap.h
 * @brief 플랫폼 VK / KeySym → Key, 그리고 Win32 폴 테이블.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/KeyCodes.h"

namespace sw
{
	/// @brief 가상 키 ↔ 엔진 Key 한 쌍
	struct InputVkKeyPair
	{
		int32 vk;
		Key	  key;
	};

	/** @brief Win32 가상 키를 Key로 변환합니다. */
	SW_API Key mapWin32VirtualKey( uintptr_t vk );
	/** @brief X11 KeySym을 Key로 변환합니다. */
	SW_API Key mapX11KeySym( uint64 keySym );
	/** @brief Win32 마우스 메시지를 MouseButton으로 변환합니다. */
	SW_API MouseButton mapWin32MouseButton( uint32 message, uintptr_t wParam );

	/** @brief Key::Unknown 항목으로 끝나는 테이블입니다 (vk는 미사용). */
	SW_API const InputVkKeyPair* getWin32PollKeyTable( uint32& outCount );
} // namespace sw
