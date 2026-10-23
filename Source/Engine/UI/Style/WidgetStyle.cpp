#include "pch.h"

#include "Engine/UI/Style/WidgetStyle.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    namespace
    {
        struct WidgetStyleInternal
        {
            using Table = UIStyleFieldTable;

            /** @brief 필드마다 플래그입니다. `UIStyleField` 순서(`WidgetStyle` PROPERTY 선언 순서)입니다. */
            static constexpr uint32 kArrFlags[] = {
                0,                                         // BackgroundColor
                0,                                         // CornerRadius
                0,                                         // BorderWidth
                0,                                         // BorderColor
                0,                                         // ShadowColor
                0,                                         // ShadowOffset
                0,                                         // ShadowBlur
                Table::kAffectsLayout,                     // Padding
                Table::kAffectsLayout | Table::kInherited, // Font
                Table::kAffectsLayout | Table::kInherited, // FontSize
                Table::kInherited,                         // TextColor
                Table::kInherited,                         // TextOutlineColor
                Table::kInherited,                         // TextOutlineWidth
                0,                                         // FocusRingColor
                Table::kAffectsSubtree,                    // Opacity
                0,                                         // Transition
            };
            static_assert( SW_COUNT_OF( kArrFlags ) == static_cast<size_t>( UIStyleField::Count ), "UIStyleField and the flag table differ" );

            static bool isSameFont( const FontSpec& lhs, const FontSpec& rhs ) { return lhs._family == rhs._family && lhs._weight == rhs._weight && lhs._slant == rhs._slant; }

            /** @brief 필드 하나의 값이 같으면 true 입니다. */
            static bool isSameValue( const WidgetStyle& lhs, const WidgetStyle& rhs, UIStyleField field )
            {
                switch ( field )
                {
                    case UIStyleField::BackgroundColor:
                        return lhs._backgroundColor == rhs._backgroundColor;
                    case UIStyleField::CornerRadius:
                        return lhs._cornerRadius == rhs._cornerRadius;
                    case UIStyleField::BorderWidth:
                        return lhs._borderWidth == rhs._borderWidth;
                    case UIStyleField::BorderColor:
                        return lhs._borderColor == rhs._borderColor;
                    case UIStyleField::ShadowColor:
                        return lhs._shadowColor == rhs._shadowColor;
                    case UIStyleField::ShadowOffset:
                        return lhs._shadowOffset == rhs._shadowOffset;
                    case UIStyleField::ShadowBlur:
                        return lhs._shadowBlur == rhs._shadowBlur;
                    case UIStyleField::Padding:
                        return lhs._padding == rhs._padding;
                    case UIStyleField::Font:
                        return isSameFont( lhs._font, rhs._font );
                    case UIStyleField::FontSize:
                        return lhs._fontSize == rhs._fontSize;
                    case UIStyleField::TextColor:
                        return lhs._textColor == rhs._textColor;
                    case UIStyleField::TextOutlineColor:
                        return lhs._textOutlineColor == rhs._textOutlineColor;
                    case UIStyleField::TextOutlineWidth:
                        return lhs._textOutlineWidth == rhs._textOutlineWidth;
                    case UIStyleField::FocusRingColor:
                        return lhs._focusRingColor == rhs._focusRingColor;
                    case UIStyleField::Opacity:
                        return lhs._opacity == rhs._opacity;
                    case UIStyleField::Transition:
                        return lhs._transition == rhs._transition;
                    case UIStyleField::Count:
                        return true;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 UIStyleFieldTable::getFlags( UIStyleField field )
    {
        const uint32 index = static_cast<uint32>( field );
        return index < static_cast<uint32>( UIStyleField::Count ) ? WidgetStyleInternal::kArrFlags[index] : 0u;
    }

    void UIStyleFieldTable::storeFloat4( const float4& value, float32 ( &outArr )[4] )
    {
        outArr[0] = value._x;
        outArr[1] = value._y;
        outArr[2] = value._z;
        outArr[3] = value._w;
    }

    bool UIStyleFieldTable::tryFindField( string_view name, UIStyleField& outField )
    {
        const vector<PropertyInfo>& listProperty = WidgetStyle::StaticType()->getPropertiesWithBase();
        const uint32                fieldCount   = MathUtil::min( static_cast<uint32>( listProperty.size() ), static_cast<uint32>( UIStyleField::Count ) );
        for ( uint32 index = 0; index < fieldCount; ++index )
        {
            if ( StringUtil::equals( name, string_view( listProperty[index]._name.c_str() ), true ) )
            {
                outField = static_cast<UIStyleField>( index );
                return true;
            }
        }
        return false;
    }

    uint32 UIComputedStyle::computeChangedFields( const UIComputedStyle* pOld, const UIComputedStyle* pNew )
    {
        if ( pOld == pNew )
            return 0;
        // 한쪽이 없으면 다른 쪽이 정한 필드가 모두 바뀐 필드다. 빈 스타일 정적 객체는 두지 않는다. 텍스트 필드(폰트, 전환)가 힙을 잡아
        // 처음 부른 프레임의 태그(UI)로 프로세스 끝까지 남기 때문이다(AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline).
        if ( pOld == nullptr || pNew == nullptr )
            return ( pOld != nullptr ? pOld->_setMask : 0u ) | ( pNew != nullptr ? pNew->_setMask : 0u );
        const UIComputedStyle& oldStyle = *pOld;
        const UIComputedStyle& newStyle = *pNew;
        uint32                 changed  = oldStyle._setMask ^ newStyle._setMask;
        const uint32           both     = oldStyle._setMask & newStyle._setMask;
        for ( uint32 index = 0; index < static_cast<uint32>( UIStyleField::Count ); ++index )
        {
            const UIStyleField field = static_cast<UIStyleField>( index );
            const uint32       bit   = UIStyleFieldTable::makeBit( field );
            if ( ( both & bit ) != 0 && WidgetStyleInternal::isSameValue( oldStyle._value, newStyle._value, field ) == false )
                changed |= bit;
        }
        return changed;
    }
} // namespace sw
