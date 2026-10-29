/**
 * @file HeadlessWindowPlatform.cpp
 * @brief 창이 없는 리눅스 전용 서버 빌드의 창 플랫폼을 정의합니다. 네이티브 창 이벤트 판정을 둡니다.
 * @details X11 구현(`Linux/X11NativeWindowEvent.cpp`)은 서버 빌드에 없다. 서버는 창을 만들지 않으므로
 *          이벤트가 오지 않고, 개발 콘솔은 표준 입력(서버 콘솔)으로 받는다.
 */
#include "pch.h"

#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_LINUX ) && !defined( SW_WITH_CLIENT_CODE )

namespace sw
{
    bool NativeWindowEvent::isMouseInput() const
    {
        return false;
    }

    bool NativeWindowEvent::isKeyboardInput() const
    {
        return false;
    }

    bool NativeWindowEvent::isInputRelease() const
    {
        return false;
    }
} // namespace sw

#endif
