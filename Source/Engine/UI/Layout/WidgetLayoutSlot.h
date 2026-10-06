/**
 * @file WidgetLayoutSlot.h
 * @brief 부모 패널이 위젯을 놓는 규칙(슬롯)과 레이아웃 열거형입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 슬롯 안 정렬입니다. Fill 은 슬롯 크기 그대로, 나머지는 원하는 크기로 그 자리에. */
    ENUM()
    enum class UiAlignment : uint8
    {
        Fill,
        Start,
        Center,
        End
    };

    /** @brief 상자 패널의 주축 크기 규칙입니다(UMG Size Rule · Godot Expand). */
    ENUM()
    enum class UiSizeRule : uint8
    {
        Auto, ///< 원하는 크기만큼
        Fill  ///< 남은 주축을 채우기 비(_fillWeight)로 나눠 받는다
    };

    /** @brief 앵커 크기가 원하는 크기를 따를 때 커지는 쪽입니다(Godot grow_direction). */
    ENUM()
    enum class UiGrowDirection : uint8
    {
        End,   ///< 왼쪽(위) 변을 두고 오른쪽(아래)으로
        Begin, ///< 오른쪽(아래) 변을 두고 왼쪽(위)으로
        Both   ///< 가운데를 두고 양쪽으로
    };

    /** @brief 상자 · 흐름 패널의 주축 방향입니다. */
    ENUM()
    enum class UiOrientation : uint8
    {
        Horizontal,
        Vertical
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WidgetLayoutSlot
     * @brief 부모가 이 위젯을 놓는 규칙입니다 — 모든 패널의 칸을 하나에 모았고, 부모는 자기 칸만 읽습니다(Godot 컨트롤의 앵커 · 크기 플래그 방식).
     * @details 상자(Box) · 겹침(Overlay) · 격자(Grid) · 흐름(Wrap): 여백 · 정렬. 상자: 크기 규칙 · 채우기 비. 캔버스: 앵커 · 오프셋 · 자동 크기 ·
     *          커지는 쪽 · z 순서. 격자: 행 · 열 · 넓이. 모든 패널: 크기 덮어쓰기 · 최소 · 최대(UMG SizeBox 를 위젯 칸으로). 길이는 모두 UI 단위입니다.
     */
    REFLECT()
    struct SW_API WidgetLayoutSlot
    {
        REFLECT_BODY();

        // 공통
        PROPERTY( DisplayName = "Padding", Tooltip = "Left, top, right, bottom", Meta = "Units=ui" )
        float4 _padding{};
        PROPERTY( DisplayName = "Horizontal Alignment" )
        UiAlignment _horizontalAlignment{ UiAlignment::Fill };
        PROPERTY( DisplayName = "Vertical Alignment" )
        UiAlignment _verticalAlignment{ UiAlignment::Fill };
        PROPERTY( DisplayName = "Width Override", Tooltip = "<= 0 means none", Meta = "Units=ui" )
        float32 _widthOverride{ 0.0f };
        PROPERTY( DisplayName = "Height Override", Tooltip = "<= 0 means none", Meta = "Units=ui" )
        float32 _heightOverride{ 0.0f };
        PROPERTY( DisplayName = "Min Size", Meta = "Units=ui" )
        float2 _minSize{};
        PROPERTY( DisplayName = "Max Size", Tooltip = "0 means unbounded", Meta = "Units=ui" )
        float2 _maxSize{};
        // 상자
        PROPERTY( DisplayName = "Size Rule" )
        UiSizeRule _sizeRule{ UiSizeRule::Auto };
        PROPERTY( DisplayName = "Fill Weight", Min = 0.0 )
        float32 _fillWeight{ 1.0f };
        // 캔버스(Godot 식 — 변 = 앵커 × 부모 크기 + 오프셋)
        PROPERTY( DisplayName = "Anchor Min" )
        float2 _anchorMin{};
        PROPERTY( DisplayName = "Anchor Max" )
        float2 _anchorMax{};
        PROPERTY( DisplayName = "Offset Min", Tooltip = "Left and top edges from the min anchor", Meta = "Units=ui" )
        float2 _offsetMin{};
        PROPERTY( DisplayName = "Offset Max", Tooltip = "Right and bottom edges from the max anchor", Meta = "Units=ui" )
        float2 _offsetMax{ 100.0f, 30.0f };
        PROPERTY( DisplayName = "Auto Size", Tooltip = "Use the desired size instead of the max offset" )
        bool _bAutoSize{ false };
        PROPERTY( DisplayName = "Grow Horizontal" )
        UiGrowDirection _growHorizontal{ UiGrowDirection::End };
        PROPERTY( DisplayName = "Grow Vertical" )
        UiGrowDirection _growVertical{ UiGrowDirection::End };
        PROPERTY( DisplayName = "Z Order" )
        int16 _zOrder{ 0 };
        // 격자
        PROPERTY( DisplayName = "Row" )
        uint16 _row{ 0 };
        PROPERTY( DisplayName = "Column" )
        uint16 _column{ 0 };
        PROPERTY( DisplayName = "Row Span", Min = 1 )
        uint16 _rowSpan{ 1 };
        PROPERTY( DisplayName = "Column Span", Min = 1 )
        uint16 _columnSpan{ 1 };

        /** @brief 두 축 모두 크기 덮어쓰기가 있으면 true 입니다 — 원하는 크기가 자식에 기대지 않는다(레이아웃 경계). */
        bool hasFixedSize() const { return _widthOverride > 0.0f && _heightOverride > 0.0f; }
    };
} // namespace sw
