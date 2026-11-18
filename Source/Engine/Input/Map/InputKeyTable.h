/**
 * @file InputKeyTable.h
 * @brief 플랫폼 VK/KeySym 을 Key 로 변환하고, Win32 폴링 테이블을 제공합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/KeyCodeUtil.h"

namespace sw
{
    /** @brief 플랫폼 키와 마우스 코드를 엔진 Key/MouseButton 으로 바꿉니다. */
    struct SW_API InputKeyTable
    {
        /** @brief 가상 키와 엔진 Key 의 한 쌍입니다. */
        struct VkKeyPair
        {
            int32 _vk;
            Key   _key;
        };

        /** @brief Win32 가상 키와 lParam 스캔코드를 Key 로 바꿉니다. */
        static Key mapWin32VirtualKey( uintptr_t vk, intptr_t lParam = 0 );
        /** @brief X11 KeySym 을 Key 로 바꿉니다. */
        static Key mapX11KeySym( uint64 keySym );
        /**
         * @brief X11 · Wayland 키 이벤트의 키심과 키 코드를 Key 로 바꿉니다. 키 이벤트를 읽는 곳은 모두 이 함수를 쓴다.
         * @details 표에 있는 키심이면 그 키이고, 아니면(러시아어 배열의 Cyrillic_ef, 프랑스어 배열의 숫자 줄 ampersand) 키 코드의 물리 자리로 읽는다 - 표에 없는 키심을 `Key::Unknown` 으로 두면
         *          그 배열에서 WASD · 단축키가 통째로 죽는다. Win32 가 비라틴 배열에서도 라틴 VK 를 주는 것, GLFW 가 키 코드 표를 쓰는 것과 같은 자리다.
         */
        static Key mapX11Key( uint64 keySym, uint32 keyCode );
        /**
         * @brief Android 키 번호(`AKEYCODE_*`)를 Key 로 바꿉니다. 엔진 키가 없으면(볼륨 · 음소거 · 카메라 · 미디어) `Key::Unknown` 입니다.
         * @details 창 글루(`AndroidWindow::onInputEvent`)가 이 답으로 이벤트를 소비할지 정한다 — 옮기지 못한 키는 시스템에 돌려줘야 볼륨 버튼이 산다(SDL 이 볼륨 키에 0 을 돌려주는 것과 같다).
         *          표는 모든 플랫폼에서 짓는다(값은 `AndroidKeyCode`) — 데스크톱 단위 테스트(PlatformInputTest)가 본다.
         */
        static Key mapAndroidKeyCode( int32 keyCode );
        /**
         * @brief Android 패드 장치의 키 번호를 엔진 패드 버튼으로 바꿉니다. 엔진 버튼이 없는 키(L2 · R2 디지털 — 방아쇠는 축이 준다, 모드 키)는 Count 입니다.
         * @details https://developer.android.com/develop/ui/views/touch-and-input/game-controllers/controller-input — 얼굴 버튼 BUTTON_A/B/X/Y, 어깨 L1/R1, 스틱 누름 THUMBL/R, START · SELECT,
         *          D패드는 패드에 따라 DPAD 키 또는 HAT 축으로 온다(둘 다 받는다).
         */
        static GamepadButton mapAndroidGamepadKeyCode( int32 keyCode );
        /**
         * @brief 웹 키 이벤트의 DOM `KeyboardEvent.code`(물리 자리 — "KeyA" · "ArrowLeft")를 Key 로 바꿉니다. 모르는 code 는 `Key::Unknown` 입니다.
         * @details 엔진 키 이름과 다른 것만 표에 두고 나머지(KeyA → A, Digit1, F1, Enter, Numpad0 …)는 이름 규칙으로 맞춘다. https://www.w3.org/TR/uievents-code/
         */
        static Key mapWebKeyCode( string_view domCode );
        /** @brief Win32 마우스 메시지를 MouseButton 으로 바꿉니다. */
        static MouseButton mapWin32MouseButton( uint32 message, uintptr_t wParam );

        /** @brief GetAsyncKeyState 로 폴링할 가상 키와 Key 의 테이블을 반환합니다. 항목 수는 @p outCount 에 담깁니다(Windows 가 아니면 nullptr 과 0). */
        static const VkKeyPair* getWin32PollKeyTable( uint32& outCount );
    };
} // namespace sw
