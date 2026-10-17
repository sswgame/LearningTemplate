#include "pch.h"

#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_LINUX )
namespace sw
{
    // X11 클립보드는 선택(CLIPBOARD) 소유와 SelectionRequest 응답이 창 메시지 루프에 있어야 한다 — 아직 없다(백로그 D6-2).
    // UI 는 엔진 안 클립보드를 쓰고, 호스트는 `isAvailable` 을 보고 끼우지 않는다.
    bool PlatformClipboard::isAvailable()
    {
        return false;
    }

    bool PlatformClipboard::readText( [[maybe_unused]] string& outText )
    {
        return false;
    }

    void PlatformClipboard::writeText( [[maybe_unused]] string_view text )
    {
    }
} // namespace sw
#endif
