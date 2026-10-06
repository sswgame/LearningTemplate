/**
 * @file WidgetNavigation.h
 * @brief 위젯 하나의 포커스 탐색 규칙입니다(방향마다 규칙 · 명시 이웃).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 탐색 방향입니다. Next · Previous 는 탭 순서(문서 순서)입니다. */
    ENUM()
    enum class UiNavigationDirection : uint8
    {
        Up,
        Down,
        Left,
        Right,
        Next,
        Previous
    };
} // namespace sw

namespace sw
{
    /** @brief 한 방향의 규칙입니다(언리얼 EUINavigationRule). */
    ENUM()
    enum class UiNavigationRule : uint8
    {
        Escape,  ///< 여기서 정하지 않는다 — 조상에게 넘긴다(기본)
        Stop,    ///< 이 위젯(패널이면 그 안)에서만 찾고, 없으면 그 자리에 선다
        Wrap,    ///< 이 패널 안에서 찾고, 없으면 반대쪽 끝으로 돈다
        Explicit ///< 이름으로 정한 위젯으로 간다(Godot focus_neighbor). 받을 수 없으면 다음 규칙으로
    };
} // namespace sw

namespace sw
{
    /** @struct WidgetNavigationEntry @brief 한 방향의 규칙과 명시 이웃입니다. */
    REFLECT()
    struct SW_API WidgetNavigationEntry
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Rule" )
        UiNavigationRule _rule{ UiNavigationRule::Escape };
        PROPERTY( DisplayName = "Target", Tooltip = "Widget name for the Explicit rule" )
        hashed_string _target{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WidgetNavigation
     * @brief 이 위젯(패널이면 그 안)에서 방향마다 포커스가 어디로 가는지입니다. 모두 Escape 면 공간 탐색이 화면 전체에서 고릅니다.
     */
    REFLECT()
    struct SW_API WidgetNavigation
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Up" )
        WidgetNavigationEntry _up{};
        PROPERTY( DisplayName = "Down" )
        WidgetNavigationEntry _down{};
        PROPERTY( DisplayName = "Left" )
        WidgetNavigationEntry _left{};
        PROPERTY( DisplayName = "Right" )
        WidgetNavigationEntry _right{};
        PROPERTY( DisplayName = "Next" )
        WidgetNavigationEntry _next{};
        PROPERTY( DisplayName = "Previous" )
        WidgetNavigationEntry _previous{};

        const WidgetNavigationEntry& getEntry( UiNavigationDirection direction ) const;
    };
} // namespace sw
