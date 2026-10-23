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
     * @details Windows 는 `CF_UNICODETEXT` 를 쓴다. 쓰기는 주 창(`MainWindowSubsystem::getWindow`)을 클립보드 주인으로 둔다 — 주인 없이 비우면 `SetClipboardData` 가 실패한다.
     *          X11 은 CLIPBOARD 선택이다. 쓰기는 활성 창을 선택 주인으로 두고 글을 창에 맡기며, 다른 프로그램의 요청(`SelectionRequest`)은 창의 메시지 루프가 답한다.
     *          읽기는 주인에게 UTF8_STRING 변환을 청하고 답을 최대 200 ms 기다린다(큰 글은 INCR 조각으로 받는다). 내줄 때도 X 요청 한 번보다 큰 글은 INCR 로 조각내 보낸다(창이 상대가 조각을 지울 때마다 다음 조각을 적는다). 창이 없는 서버 빌드는 `isAvailable` 이 false 다.
     *          Wayland 는 없다(XWayland 로 뜬 창은 X11 길을 쓴다). 메인(창) 스레드에서만 부른다.
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
