/**
 * @file CursorShape.h
 * @brief OS 마우스 커서의 모양입니다(`InputManager::setCursorShape`). 런타임 UI 가 호버한 위젯에 따라 고른다(`UICursorShape` → 이것).
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief OS 커서 모양입니다. 플랫폼마다 표준 커서로 옮긴다(Win32 `IDC_*`, X11 `XC_*`). */
    enum class CursorShape : uint8
    {
        Arrow = 0,        ///< 기본 화살표
        Hand,             ///< 누를 수 있는 것(단추 · 링크)
        TextBeam,         ///< 글을 고르거나 쓰는 곳(I 빔)
        ResizeHorizontal, ///< 좌우로 크기 바꾸기
        ResizeVertical,   ///< 위아래로 크기 바꾸기
        NotAllowed,       ///< 할 수 없음
    };
} // namespace sw
