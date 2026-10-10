#include "pch.h"

#include "Engine/UI/Style/WidgetStyle.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    namespace
    {
        struct WidgetStyleInternal
        {
            using Table = UIStyleFieldTable;

            /** @brief 칸 표 — `UIStyleField` 순서, `WidgetStyle` 선언 순서와 같다. */
            static constexpr Table::Entry kArrEntry[] = {
                { "_backgroundColor",                                         0},
                {    "_cornerRadius",                                         0},
                {     "_borderWidth",                                         0},
                {     "_borderColor",                                         0},
                {     "_shadowColor",                                         0},
                {    "_shadowOffset",                                         0},
                {      "_shadowBlur",                                         0},
                {         "_padding",                     Table::kAffectsLayout},
                {            "_font", Table::kAffectsLayout | Table::kInherited},
                {        "_fontSize", Table::kAffectsLayout | Table::kInherited},
                {       "_textColor",                         Table::kInherited},
                {"_textOutlineColor",                         Table::kInherited},
                {"_textOutlineWidth",                         Table::kInherited},
                {  "_focusRingColor",                                         0},
                {         "_opacity",                    Table::kAffectsSubtree},
                {      "_transition",                                         0},
            };
            static_assert( sizeof( kArrEntry ) / sizeof( kArrEntry[0] ) == static_cast<size_t>( UIStyleField::Count ), "UIStyleField and the table differ" );

            static bool isSameFont( const FontSpec& lhs, const FontSpec& rhs ) { return lhs._family == rhs._family && lhs._weight == rhs._weight && lhs._slant == rhs._slant; }

            /** @brief 칸 하나의 값이 같은가입니다. */
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
    const UIStyleFieldTable::Entry& UIStyleFieldTable::getEntry( UIStyleField field )
    {
        return WidgetStyleInternal::kArrEntry[static_cast<uint32>( field )];
    }

    bool UIStyleFieldTable::tryFindField( string_view name, UIStyleField& outField )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( UIStyleField::Count ); ++index )
        {
            if ( StringUtil::equals( name, string_view( WidgetStyleInternal::kArrEntry[index]._pName ), true ) )
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
        // 한쪽이 없으면 다른 쪽이 정한 칸이 모두 바뀐 칸이다. 빈 스타일 정적 객체를 두지 않는다 — 글 칸(글꼴 · 전환)이 힙을 잡아
        // 처음 부른 프레임의 태그(UI)로 프로세스 끝까지 남는다(AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline).
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
