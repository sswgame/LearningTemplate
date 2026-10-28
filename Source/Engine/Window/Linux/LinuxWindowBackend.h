/**
 * @file LinuxWindowBackend.h
 * @brief 리눅스 창 계층(X11 · Wayland)을 고르는 환경 변수 `SW_WINDOW_BACKEND` 를 읽는 한 곳입니다.
 * @details 메인 창(`IWindow::createPlatformWindow`)과 스플래시 창(`ISplashWindow`)이 같은 계층을 골라야 하므로 읽는 코드를 한 곳에 둔다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 리눅스에서 고른 창 계층입니다. */
    enum class LinuxWindowBackend : uint8
    {
        X11 = 0,
        Wayland,
    };

    /**
     * @brief `SW_WINDOW_BACKEND`(= `x11` | `wayland`, 없으면 `x11`)를 읽어 @p outBackend 에 담습니다.
     * @details 빈 값(`SW_WINDOW_BACKEND=`)은 정해진 값이라 모르는 값이다 — 없음일 때만 기본 X11 이다. 모르는 값이면 오류를 남기고 false 입니다(기본 계층으로 대신하지 않는다).
     *          이 빌드가 짓지 않은 계층(`SW_WITH_WAYLAND` 없이 `wayland`)을 거절하는 것은 호출 쪽이다.
     */
    [[nodiscard]] bool tryReadLinuxWindowBackend( LinuxWindowBackend& outBackend );
} // namespace sw
