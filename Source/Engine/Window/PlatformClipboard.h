/**
 * @file PlatformClipboard.h
 * @brief 운영체제 글 클립보드를 읽고 씁니다(창 계층). 런타임 UI 의 클립보드 창구(`UIClipboardAPI`)에 호스트가 끼웁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @struct PlatformClipboard
     * @brief 운영체제 클립보드의 UTF-8 글 창구입니다. 언리얼 `FPlatformApplicationMisc::ClipboardCopy/Paste`, Godot `DisplayServer.clipboard_get/set` 자리다.
     * @details Windows 는 `CF_UNICODETEXT` 를 쓴다. 쓰기는 활성 창(`IWindow::getActiveWindow`)을 클립보드 주인으로 둔다 — 주인 없이 비우면 `SetClipboardData` 가 실패한다.
     *          X11 클립보드는 선택(selection) 소유와 이벤트 응답이 필요해 아직 없다(`isAvailable` 이 false). 그 플랫폼의 UI 는 엔진 안 보관본을 쓴다.
     *          메인(창) 스레드에서만 부른다.
     */
    struct SW_API PlatformClipboard
    {
        /** @brief 이 플랫폼 빌드가 운영체제 클립보드를 잇는지입니다. */
        static bool isAvailable();
        /** @brief 클립보드의 글을 @p outText 에 담습니다. 글이 없거나 열지 못하면 false 입니다. */
        [[nodiscard]] static bool readText( string& outText );
        /** @brief 클립보드에 글을 씁니다. 쓰지 못하면 경고를 남깁니다. */
        static void writeText( string_view text );
    };
} // namespace sw
