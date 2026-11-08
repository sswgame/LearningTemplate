/**
 * @file AndroidClipboard.cpp
 * @brief Android 클립보드입니다. 아직 OS 클립보드(JNI `ClipboardManager`)를 잇지 않아 `isAvailable` 이 false 다 — 호스트는 그것을 보고 끼우지 않는다.
 * @details 잇는 일은 docs/PlatformTargets.md 남은 일에 있다(ClipboardManager 는 Looper 스레드 제약이 있어 실기기 확인과 함께 넣는다).
 */
#include "pch.h"

#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_ANDROID )

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
