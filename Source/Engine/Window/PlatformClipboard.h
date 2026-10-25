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
     *          Wayland 는 `wl_data_device` 선택이다(`text/plain;charset=utf-8` 우선, 없으면 `text/plain`). 쓰기는 마지막 키 · 버튼 입력의 일련번호가 있어야 컴포지터가 받으므로, 창에 입력이 한 번도 없었으면 경고를 남기고 쓰지 않는다.
     *          읽기는 상대가 파이프에 쓰는 글을 최대 200 ms 씩 기다려 받는다(끝없는 글은 16 MiB 에서 끊는다). 창 종류별 캐스트(`tryCast`)로 활성 창을 읽고 다른 종류의 창을 읽지 않는다. 메인(창) 스레드에서만 부른다.
     */
    struct SW_API PlatformClipboard
    {
        /** @brief 운영체제 클립보드를 잇는지입니다. 리눅스 클라이언트는 활성 창이 X11 또는 Wayland 창일 때만 true 라 창을 만든 뒤에 부릅니다. */
        static bool isAvailable();
        /** @brief 클립보드의 글을 @p outText 에 담습니다. 글이 없거나 열지 못하면 false 입니다. */
        [[nodiscard]] static bool readText( string& outText );
        /** @brief 클립보드에 글을 씁니다. 쓰지 못하면 경고를 남기고 false 입니다(호출 쪽이 사용자에게 알린다: `UIClipboardService::setText` → 글 입력 칸의 알림). */
        [[nodiscard]] static bool writeText( string_view text );
    };
} // namespace sw
