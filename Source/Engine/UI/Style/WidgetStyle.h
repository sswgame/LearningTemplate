/**
 * @file WidgetStyle.h
 * @brief 위젯 겉모습의 칸(배경 · 모서리 · 테두리 · 그림자 · 여백 · 글꼴 · 글 색 · 외곽선 · 포커스 테두리 · 불투명도 · 전환)과 계산된 스타일입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Text/FontSystem.h"

namespace sw
{
    /**
     * @struct WidgetStyle
     * @brief 스타일 시트 규칙이 쓰는 겉모습 칸입니다 — 규칙(`<Rule _selector="…" _backgroundColor="…" />`)의 속성 이름이 곧 이 PROPERTY 이름입니다.
     * @details 칸의 순서 · 뜻은 `UiStyleField` · `UiStyleFieldTable` 과 짝입니다(`UiStyleTest.FieldTableMatchesReflection` 이 지킨다). 길이는 UI 단위입니다.
     */
    REFLECT()
    struct SW_API WidgetStyle
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Background Color" )
        float4 _backgroundColor{};
        PROPERTY( DisplayName = "Corner Radius", Tooltip = "Top-left, top-right, bottom-right, bottom-left", Meta = "Units=ui" )
        float4 _cornerRadius{};
        PROPERTY( DisplayName = "Border Width", Min = 0.0, Meta = "Units=ui" )
        float32 _borderWidth{ 0.0f };
        PROPERTY( DisplayName = "Border Color" )
        float4 _borderColor{};
        PROPERTY( DisplayName = "Shadow Color" )
        float4 _shadowColor{};
        PROPERTY( DisplayName = "Shadow Offset", Meta = "Units=ui" )
        float2 _shadowOffset{};
        PROPERTY( DisplayName = "Shadow Blur", Min = 0.0, Meta = "Units=ui" )
        float32 _shadowBlur{ 0.0f };
        PROPERTY( DisplayName = "Padding", Tooltip = "Inner padding of borders and buttons (left, top, right, bottom)", Meta = "Units=ui" )
        float4 _padding{};
        PROPERTY( DisplayName = "Font" )
        FontSpec _font{};
        PROPERTY( DisplayName = "Font Size", Min = 1.0, Meta = "Units=ui" )
        float32 _fontSize{ 18.0f };
        PROPERTY( DisplayName = "Text Color" )
        float4 _textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( DisplayName = "Text Outline Color" )
        float4 _textOutlineColor{};
        PROPERTY( DisplayName = "Text Outline Width", Min = 0.0, Meta = "Units=ui" )
        float32 _textOutlineWidth{ 0.0f };
        PROPERTY( DisplayName = "Focus Ring Color" )
        float4 _focusRingColor{ 1.0f, 0.78f, 0.2f, 1.0f };
        PROPERTY( DisplayName = "Opacity", Min = 0.0, Max = 1.0, Tooltip = "Multiplies the widget's own opacity" )
        float32 _opacity{ 1.0f };
        PROPERTY( DisplayName = "Transition", Tooltip = "Property Seconds Curve, ... (style transitions)" )
        string _transition{};
    };
} // namespace sw

namespace sw
{
    /** @brief `WidgetStyle` 의 칸 번호입니다(선언 순서와 같다 — 계산된 스타일의 "정한 칸" 비트 자리). */
    enum class UiStyleField : uint8
    {
        BackgroundColor,
        CornerRadius,
        BorderWidth,
        BorderColor,
        ShadowColor,
        ShadowOffset,
        ShadowBlur,
        Padding,
        Font,
        FontSize,
        TextColor,
        TextOutlineColor,
        TextOutlineWidth,
        FocusRingColor,
        Opacity,
        Transition,
        Count
    };

    /**
     * @struct UiStyleFieldTable
     * @brief 칸마다 이름 · 바뀌면 무엇을 다시 하는가 · 물려받는가입니다(CSS 의 상속 속성 · 레이아웃에 닿는 속성).
     */
    struct SW_API UiStyleFieldTable
    {
        static constexpr uint32 kAffectsLayout  = SW_BIT( 0 ); ///< 바뀌면 원하는 크기가 바뀐다(여백 · 글꼴 · 크기)
        static constexpr uint32 kAffectsSubtree = SW_BIT( 1 ); ///< 자손 그림에 구워진다(불투명도) — 자손까지 다시 칠한다
        static constexpr uint32 kInherited      = SW_BIT( 2 ); ///< 부모의 계산된 값을 물려받는다(글 칸)

        struct Entry
        {
            const utf8* _pName;
            uint32      _flags;
        };

        /** @brief 칸 하나의 표 줄입니다. */
        static const Entry& getEntry( UiStyleField field );
        /** @brief 이름(`_backgroundColor`, 대소문자 무시)의 칸입니다. 없으면 false 입니다. */
        [[nodiscard]] static bool tryFindField( string_view name, UiStyleField& outField );
        /** @brief 비트 하나(칸 하나)입니다. */
        static constexpr uint32 makeBit( UiStyleField field ) { return 1u << static_cast<uint32>( field ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiComputedStyle
     * @brief 위젯 하나의 계산된 스타일입니다 — 값과 "규칙이 정했거나 물려받은 칸" 비트. 정하지 않은 칸은 위젯 자기 칸(코드 · 문서가 넣은 기본 겉모습)을 씁니다.
     * @details 같은 규칙 묶음 · 같은 부모 스타일인 위젯은 한 객체를 나눠 씁니다(`UiStyleSet`). 값은 바꾸지 않습니다.
     */
    struct SW_API UiComputedStyle
    {
        WidgetStyle _value{};
        uint32      _setMask{ 0 };

        bool has( UiStyleField field ) const { return ( _setMask & UiStyleFieldTable::makeBit( field ) ) != 0; }
        /** @brief 두 스타일에서 값이나 정함이 다른 칸 비트입니다(nullptr 은 아무 칸도 정하지 않은 것). */
        static uint32 computeChangedFields( const UiComputedStyle* pOld, const UiComputedStyle* pNew );
    };
} // namespace sw
