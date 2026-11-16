/**
 * @file HeadlessClipboard.cpp
 * @brief 창이 없는 리눅스 전용 서버 빌드의 클립보드입니다. 클립보드를 이을 창이 없으므로 호스트는 `isAvailable` 을 보고 끼우지 않습니다.
 * @details X11 · Wayland 구현(`Linux/X11Clipboard.cpp`)은 서버 빌드에 없다(서버는 `Window/Linux` 를 짓지 않는다 — `cmake/Modules/Build/BuildLayout.cmake`).
 */
#include "pch.h"

#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_LINUX )

namespace sw
{
    bool PlatformClipboard::isAvailable()
    {
        return false;
    }

    bool PlatformClipboard::readText( [[maybe_unused]] string& outText )
    {
        return false;
    }

    bool PlatformClipboard::writeText( [[maybe_unused]] string_view text )
    {
        return false;
    }
} // namespace sw

#endif
