/**
 * @file X11Headers.h
 * @brief Xlib 기본 헤더(Xlib · Xutil · Xatom · keysym)를 포함하고 곧바로 `X11MacroUndef.h` 로 흔한 이름의 매크로를 지웁니다.
 * @details X11 은 `Convex` · `None` · `Success` · `KeyPress` 같은 흔한 단어를 매크로로 정의합니다. 이 헤더가 PCH 에 들어가면 엔진의 모든 TU
 *          (서드파티 헤더를 포함하는 TU 까지)에 그 매크로가 퍼집니다 — Jolt 의 `EShapeType::Convex` 가 그렇게 깨졌습니다.
 *          그래서 X11 헤더는 **X11 을 직접 쓰는 `.cpp` 에서만** include 합니다(`CheckX11Isolation.py`). 유니티 빌드에서는 그 `.cpp` 를
 *          묶음에서 빼(`sw_setUnityBuild`) 매크로가 이웃 TU 로 새지 않게 합니다.
 *          GLX · XKB · Xlib-xcb 처럼 더 필요한 X11 헤더는 이 헤더 뒤에 include 하고, 그 뒤에 `X11MacroUndef.h` 를 다시 include 합니다.
 * @note 리눅스 전용입니다. 다른 플랫폼에서는 아무것도 하지 않습니다.
 */
#pragma once

#if defined( SW_PLATFORM_LINUX )
    #include <X11/Xatom.h>
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/keysym.h>

    #include "Core/Common/X11MacroUndef.h"
#endif
