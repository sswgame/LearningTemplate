/**
 * @file InputKeyMap.h
 * @brief 플랫폼 VK / KeySym → Key 변환과 Win32 폴링 표입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/KeyCodes.h"

namespace sw
{
    /** @brief 플랫폼 키 · 마우스 코드를 엔진 Key/MouseButton 으로 바꿉니다. */
    struct SW_API InputKeyMap
    {
        /** @brief 가상 키 ↔ 엔진 Key 한 쌍입니다. */
        struct VkKeyPair
        {
            int32 _vk;
            Key   _key;
        };

        /** @brief Win32 가상 키와 lParam 스캔코드를 Key 로 바꿉니다. */
        static Key mapWin32VirtualKey( uintptr_t vk, intptr_t lParam = 0 );
        /** @brief 물리 스캔코드(ScanCode)를 물리적 위치 기준 Key 로 바꿉니다(AZERTY/QWERTZ 등 다국어 배열 호환). */
        static Key mapScanCodeToKey( uint32 scanCode, bool bExtended = false );
        /** @brief X11 KeySym 을 Key 로 바꿉니다. */
        static Key mapX11KeySym( uint64 keySym );
        /** @brief Win32 마우스 메시지를 MouseButton 으로 바꿉니다. */
        static MouseButton mapWin32MouseButton( uint32 message, uintptr_t wParam );

        /** @brief GetAsyncKeyState 로 폴링할 가상 키 ↔ Key 표를 반환합니다. 항목 수는 @p outCount 에 담깁니다(Windows 가 아니면 nullptr 과 0). */
        static const VkKeyPair* getWin32PollKeyTable( uint32& outCount );
    };
} // namespace sw
