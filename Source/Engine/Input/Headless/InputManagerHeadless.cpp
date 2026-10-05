/**
 * @file InputManagerHeadless.cpp
 * @brief 창이 없는 리눅스 전용 서버 빌드의 입력 플랫폼 훅입니다 — 장치 이벤트가 오지 않으므로 모두 비어 있습니다.
 * @details X11 구현(`Linux/InputManagerX11.cpp`)은 서버 빌드에 없습니다(서버 기계에 X 가 없어 libX11 을 링크하지 않는다).
 *          Windows 서버 빌드는 Win32 구현을 그대로 둡니다 — user32 는 모든 Windows Server 에 있고, 서버는 창 메시지를 받지 않습니다.
 */
#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_LINUX ) && !defined( SW_WITH_CLIENT_CODE )

namespace sw
{
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
        return false;
    }

    void InputManager::disableWindowsAccessibilityShortcuts()
    {
    }

    void InputManager::restoreWindowsAccessibilityShortcuts()
    {
    }
} // namespace sw

#endif
