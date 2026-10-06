/**
 * @file WidgetNavigation.h
 * @brief 위젯 하나의 포커스 탐색 규칙입니다(방향마다 규칙 · 명시 이웃).
 */
#pragma once
#include "Core/Common/Macros.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct WidgetNavigation
     * @brief 이 위젯(패널이면 그 안)에서 방향마다 포커스가 어디로 가는지입니다. 칸은 포커스 · 공간 탐색이 채웁니다.
     */
    REFLECT()
    struct SW_API WidgetNavigation
    {
        REFLECT_BODY();
    };
} // namespace sw
