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
    /** @brief 플랫폼 키·마우스 코드를 엔진 Key/MouseButton으로 변환합니다. */
    struct SW_API InputKeyMap
    {
        /** @brief 가상 키 ↔ 엔진 Key 한 쌍 */
        struct VkKeyPair
        {
            int32 _vk;
            Key   _key;
        };

        /** @brief Win32 가상 키 및 lParam 스캔코드를 Key로 변환합니다. */
        static Key mapWin32VirtualKey( uintptr_t vk, intptr_t lParam = 0 );
        /** @brief 물리 스캔코드(ScanCode)를 물리적 위치 기준 Key로 변환합니다 (AZERTY/QWERTZ 다국어 호환). */
        static Key mapScanCodeToKey( uint32 scanCode, bool bExtended = false );
        /** @brief X11 KeySym을 Key로 변환합니다. */
        static Key mapX11KeySym( uint64 keySym );
        /** @brief Win32 마우스 메시지를 MouseButton으로 변환합니다. */
        static MouseButton mapWin32MouseButton( uint32 message, uintptr_t wParam );

        /** @brief Key::Unknown 항목으로 끝나는 테이블입니다 (vk는 미사용). */
        static const VkKeyPair* getWin32PollKeyTable( uint32& outCount );
    };
} // namespace sw
