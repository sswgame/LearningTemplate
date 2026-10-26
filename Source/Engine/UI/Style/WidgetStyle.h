/**
 * @file WidgetStyle.h
 * @brief 위젯 겉모습 필드(배경, 모서리, 테두리, 그림자, 여백, 글꼴, 글자 색, 외곽선, 포커스 테두리, 불투명도, 전환)와 계산된 스타일을 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Text/FontService.h"

#include <cstddef>
#include <type_traits>

namespace sw
{
    /**
     * @struct WidgetStyle
     * @brief 스타일 시트 규칙이 쓰는 겉모습 필드입니다. 규칙(`<Rule _selector="…" _backgroundColor="…" />`)의 속성 이름이 곧 이 PROPERTY 이름입니다.
     * @details 필드의 순서와 의미는 `UIStyleField` 와 `UIStyleFieldTable` 과 짝이다(`UIStyleTest.FieldTableMatchesReflection` 이 확인한다). 길이는 UI 단위다.
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
    /**
     * @brief `WidgetStyle` 의 필드 번호입니다(PROPERTY 선언 순서와 같으며, 계산된 스타일의 "정한 필드" 비트 위치입니다).
     * @details 필드 이름(`_backgroundColor`)은 리플렉션 PROPERTY 이름이 기준이다 — `_` + 열거자 이름(첫 글자 소문자)과 같아야 한다
     *          (`UIStyleTest.FieldTableMatchesReflection`).
     */
    ENUM( Count = Count )
    enum class UIStyleField : uint8
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
     * @struct UIStyleFieldTable
     * @brief 필드마다 바뀌면 무엇을 다시 하는지, 물려받는지를 정의한 서술자 표입니다(CSS 의 상속 속성과 레이아웃에 닿는 속성에 해당).
     * @details 이름은 표에 두지 않는다 — `WidgetStyle` 리플렉션의 PROPERTY 이름을 읽는다(`tryFindField`).
     *          줄마다 플래그 · 멤버 오프셋 · 성분 수가 있어 수치 필드의 읽기 · 쓰기 · 같음 · 성분 수는 표에서 나온다. 수치가 아닌 필드(폰트 · 전환)는 성분 수가 0 이다.
     */
    struct SW_API UIStyleFieldTable
    {
        static constexpr uint32 kAffectsLayout  = SW_BIT( 0 ); ///< 바뀌면 원하는 크기가 바뀝니다(여백, 글꼴, 크기).
        static constexpr uint32 kAffectsSubtree = SW_BIT( 1 ); ///< 자손 그림에 반영됩니다(불투명도). 자손까지 다시 칠합니다.
        static constexpr uint32 kInherited      = SW_BIT( 2 ); ///< 부모의 계산된 값을 물려받습니다(텍스트 필드).

        /** @brief 필드 하나의 플래그(`kAffectsLayout` · `kAffectsSubtree` · `kInherited`)입니다. */
        static uint32 getFlags( UIStyleField field );
        /** @brief 필드의 float32 성분 수입니다(4 · 2 · 1, 보간되지 않는 필드는 0). */
        static uint32 getComponentCount( UIStyleField field );
        /** @brief 수치 필드의 값을 성분으로 읽습니다. 성분 수 뒤의 칸과 수치가 아닌 필드는 건드리지 않습니다. */
        static void readComponents( const WidgetStyle& style, UIStyleField field, float32 ( &outArr )[4] );
        /** @brief 성분을 수치 필드에 씁니다. 수치가 아닌 필드는 건드리지 않습니다. */
        static void writeComponents( WidgetStyle& style, UIStyleField field, const float32 ( &arr )[4] );
        /** @brief `WidgetStyle` 의 PROPERTY 이름(`_backgroundColor`, 철자 그대로)에 해당하는 필드를 찾습니다. 없으면 false 입니다. */
        [[nodiscard]] static bool tryFindField( string_view name, UIStyleField& outField );
        /** @brief 필드 하나의 값을 @p source 에서 @p outTarget 으로 옮깁니다(수치 필드는 표의 성분으로, 폰트 · 전환은 값째). 상속(`UIStyleSet`)이 씁니다. */
        static void copyField( const WidgetStyle& source, UIStyleField field, WidgetStyle& outTarget );
        /** @brief 비트 하나(칸 하나)입니다. */
        static constexpr uint32 makeBit( UIStyleField field ) { return 1u << static_cast<uint32>( field ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 필드 번호 → `WidgetStyle` 멤버입니다(형식 `Type`, 멤버 포인터 `kMember`, 오프셋 `kOffset`).
     * @details 서술자 표의 오프셋 · 성분 수와 `UIOwnStyle::set` 의 형식 검사가 이 한 곳에서 나온다 — 줄 하나에 필드와 멤버를 한 번씩 적어 둘이 어긋날 수 없다.
     */
    template <UIStyleField Field>
    struct UIStyleFieldMember;

#define SW_UI_STYLE_FIELD_MEMBER( field, member )                                       \
    template <>                                                                         \
    struct UIStyleFieldMember<UIStyleField::field>                                      \
    {                                                                                   \
        using Type                                   = decltype( WidgetStyle::member ); \
        static constexpr Type WidgetStyle::* kMember = &WidgetStyle::member;            \
        static constexpr size_t              kOffset = offsetof( WidgetStyle, member ); \
    }

    SW_UI_STYLE_FIELD_MEMBER( BackgroundColor, _backgroundColor );
    SW_UI_STYLE_FIELD_MEMBER( CornerRadius, _cornerRadius );
    SW_UI_STYLE_FIELD_MEMBER( BorderWidth, _borderWidth );
    SW_UI_STYLE_FIELD_MEMBER( BorderColor, _borderColor );
    SW_UI_STYLE_FIELD_MEMBER( ShadowColor, _shadowColor );
    SW_UI_STYLE_FIELD_MEMBER( ShadowOffset, _shadowOffset );
    SW_UI_STYLE_FIELD_MEMBER( ShadowBlur, _shadowBlur );
    SW_UI_STYLE_FIELD_MEMBER( Padding, _padding );
    SW_UI_STYLE_FIELD_MEMBER( Font, _font );
    SW_UI_STYLE_FIELD_MEMBER( FontSize, _fontSize );
    SW_UI_STYLE_FIELD_MEMBER( TextColor, _textColor );
    SW_UI_STYLE_FIELD_MEMBER( TextOutlineColor, _textOutlineColor );
    SW_UI_STYLE_FIELD_MEMBER( TextOutlineWidth, _textOutlineWidth );
    SW_UI_STYLE_FIELD_MEMBER( FocusRingColor, _focusRingColor );
    SW_UI_STYLE_FIELD_MEMBER( Opacity, _opacity );
    SW_UI_STYLE_FIELD_MEMBER( Transition, _transition );
#undef SW_UI_STYLE_FIELD_MEMBER
} // namespace sw

namespace sw
{
    /**
     * @class UIOwnStyle
     * @brief 위젯이 자기 필드로 그리는 겉모습과 그 필드 비트입니다(`Widget::fillOwnStyle` 의 출력).
     * @details 값은 `set<UIStyleField::X>( value )` 로만 쓴다 — 값 대입과 비트 기록이 한 번에 일어나 "값만 쓰고 비트를 빠뜨려 전환 없이 바뀌는" 경우가 없고,
     *          @p value 의 형식이 그 필드의 `WidgetStyle` 멤버 형식과 정확히 같아야 컴파일된다.
     */
    class UIOwnStyle
    {
    public:
        template <UIStyleField Field, typename T>
        void set( const T& value )
        {
            using Member = UIStyleFieldMember<Field>;
            static_assert( std::is_same_v<T, typename Member::Type>, "the value type must match the WidgetStyle member of this style field" );
            _value.*Member::kMember = value;
            _setMask |= UIStyleFieldTable::makeBit( Field );
        }
        bool               has( UIStyleField field ) const { return ( _setMask & UIStyleFieldTable::makeBit( field ) ) != 0; }
        const WidgetStyle& getValue() const { return _value; }

    private:
        WidgetStyle _value{};
        uint32      _setMask{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIComputedStyle
     * @brief 위젯 하나의 계산된 스타일입니다. 값과 "규칙이 정했거나 물려받은 필드" 비트를 가집니다. 정하지 않은 필드는 위젯 자기 필드(코드나 문서가 넣은 기본 겉모습)를 씁니다.
     * @details 같은 규칙 세트와 같은 부모 스타일을 가진 위젯은 한 객체를 공유한다(`UIStyleSet`). 값은 바꾸지 않는다.
     */
    struct SW_API UIComputedStyle
    {
        WidgetStyle _value{};
        uint32      _setMask{ 0 };

        bool has( UIStyleField field ) const { return ( _setMask & UIStyleFieldTable::makeBit( field ) ) != 0; }
        /** @brief 두 스타일에서 값이나 정함이 다른 필드 비트입니다(nullptr 은 아무 필드도 정하지 않은 것). */
        static uint32 computeChangedFields( const UIComputedStyle* pOld, const UIComputedStyle* pNew );
    };
} // namespace sw
