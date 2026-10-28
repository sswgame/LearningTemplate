/**
 * @file WaylandDevConsoleWindow.h
 * @brief 개발 콘솔(`IDevConsoleWindow`)의 Wayland 판입니다 — 게임 창 표면에 얹는 서브표면(`wl_subsurface`)에 CPU 로 그린 줄을 공유 메모리 버퍼로 보낸다.
 * @details X11 판은 게임 창의 자식 창에 Xft 로 그린다. Wayland 에는 자식 창도 서버 글꼴도 없어 서브표면과 `WaylandTextPainter`(시스템 글꼴의 거리장 글리프)로 그린다.
 *          서브표면은 입력 영역을 비워 포인터가 그대로 게임 창으로 간다(키보드는 원래 게임 창 것이다). 위치는 게임 창의 왼쪽 위(0, 0)이고 가로는 게임 창 너비다.
 *          글꼴이 없거나 컴포지터에 `wl_subcompositor` 가 없으면 만들지 않는다(콘솔은 그리지 않고 입력만 받는다). 선택은 `IDevConsoleWindow::createPlatform` 이 한다.
 */
#pragma once
#include "Engine/Window/DevConsoleWindow.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    class WaylandWindow;

    /** @brief @p owner(Wayland 게임 창) 위에 얹는 콘솔 창을 만듭니다. 못 만들면 이유를 남기고 nullptr 입니다. 반환한 콘솔은 @p owner 보다 먼저 지운다. */
    unique_ptr<IDevConsoleWindow> createWaylandDevConsoleWindow( WaylandWindow& owner );
} // namespace sw

#endif
