/**
 * @file WaylandShell.h
 * @brief 창(`WaylandWindow`)과 스플래시(`WaylandSplashWindow`)가 같이 쓰는 xdg-shell 연결 규칙입니다 — 핑 응답, 표면 configure 확인, 첫 configure 대기, 막지 않는 이벤트 펌프.
 * @details 예전에는 두 창이 같은 리스너 · 펌프를 따로 들고 있었다(StructureRefactor 7 절 15). 창마다 다른 것(최상위 configure · 닫기, 입력)만 각자 둔다.
 *          라이브러리 헤더는 .cpp 에만 있다.
 */
#pragma once
#include "Core/Foundation/Types.h"

struct wl_display;
struct xdg_surface;
struct xdg_wm_base;

namespace sw
{
    /** @class WaylandShell @brief xdg-shell 연결 규칙 묶음입니다(전부 static). */
    class WaylandShell
    {
    public:
        /** @brief @p pWmBase 의 핑에 응답하게 합니다(응답하지 않는 창은 컴포지터가 멈춘 것으로 본다). */
        static void answerPings( xdg_wm_base* pWmBase );
        /** @brief @p pXdgSurface 의 configure 를 받으면 확인(ack)하고 @p pbConfigured 를 true 로 둡니다. 플래그는 표면보다 오래 살아야 합니다. */
        static void acknowledgeConfigures( xdg_surface* pXdgSurface, bool* pbConfigured );
        /** @brief 첫 configure 를 기다립니다(그 전에는 그림을 붙이면 안 된다 — xdg-shell 규약). 받기 전에 연결이 끊기면 false 입니다. */
        [[nodiscard]] static bool waitForFirstConfigure( wl_display* pDisplay, const bool& bConfigured );
        /**
         * @brief 쌓인 연결 이벤트를 막지 않고 처리합니다: 쌓인 것 처리 → 읽을 준비 → 보낼 것 보냄 → 읽을 것이 있으면 읽음(없으면 취소) → 다시 처리.
         * @return 연결이 끊겼으면 false 입니다.
         */
        [[nodiscard]] static bool pollEvents( wl_display* pDisplay );
    };
} // namespace sw
