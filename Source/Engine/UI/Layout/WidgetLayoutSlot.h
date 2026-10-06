/**
 * @file WidgetLayoutSlot.h
 * @brief 부모 패널이 위젯을 놓는 규칙(슬롯)입니다. 칸은 레이아웃 단계(measure · arrange)가 채웁니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct WidgetLayoutSlot
     * @brief 부모가 이 위젯을 놓는 규칙입니다 — 모든 패널의 칸을 하나에 모으고, 부모는 자기 칸만 읽습니다.
     * @details 위젯 트리 코어는 칸이 없는 채로 둡니다. 레이아웃이 여백 · 정렬 · 크기 규칙 · 앵커 · 격자 칸을 더합니다.
     */
    REFLECT()
    struct SW_API WidgetLayoutSlot
    {
        REFLECT_BODY();
    };
} // namespace sw
