/**
 * @file InputManagerHeadless.cpp
 * @brief 창이 없는 리눅스 전용 서버 빌드의 입력 플랫폼 훅입니다. 장치 이벤트가 오지 않으므로 모두 비어 있습니다.
 * @details X11 구현(`Linux/InputManagerX11.cpp`)은 서버 빌드에 없다(서버 기계에 X 가 없어서 libX11 을 링크하지 않는다).
 *          Windows 서버 빌드는 Win32 구현을 그대로 둔다. user32 는 모든 Windows Server 에 있고, 서버는 창 메시지를 받지 않는다.
 */
#include "pch.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_LINUX ) && !defined( SW_WITH_CLIENT_CODE )

namespace sw
{
    namespace
    {
        /** @brief 하드웨어가 없는 게임패드 슬롯입니다. 가상 입력과 테스트가 넣는 이벤트만 받습니다. */
        class HeadlessGamepadDevice final : public GamepadDevice
        {
        public:
            explicit HeadlessGamepadDevice( uint32 deviceIndex )
                : GamepadDevice( deviceIndex ) {}

            void poll( [[maybe_unused]] float32 deltaTime ) override {}

        protected:
            bool isHardwareConnected() const override { return false; }
        };
    } // namespace

    void InputManager::pollPlatform()
    {
    }

    void InputManager::onNativeWindowEvent( [[maybe_unused]] const NativeWindowEvent& event )
    {
    }

    void InputManager::processNativeEvent( [[maybe_unused]] const NativeWindowEvent& event )
    {
    }

    void InputManager::registerPlatformGamepads()
    {
        // 하드웨어는 없지만 슬롯은 둔다. 가상 입력(재생, 테스트)이 게임패드 이벤트를 넣는 경로가 Windows 서버 빌드와 같아야 하기 때문이다.
        registerGamepadSlots<HeadlessGamepadDevice>();
    }

    void InputManager::setCursorVisiblePlatform( [[maybe_unused]] bool bVisible )
    {
    }

    void InputManager::applyMouseLockMode()
    {
    }

    void InputManager::recenterLockedCursorPlatform()
    {
    }

    void InputManager::releaseMouseLockMode()
    {
    }

    bool InputManager::isWindowFocusedPlatform() const
    {
        // 창이 없으면 포커스 값 그대로다(Win32 와 X11 구현의 "창 없음" 규칙과 같다. 테스트와 서버는 true).
        return _bWindowFocused == SW_TRUE;
    }

    void InputManager::disableWindowsAccessibilityShortcuts()
    {
    }

    void InputManager::restoreWindowsAccessibilityShortcuts()
    {
    }
} // namespace sw

#endif
