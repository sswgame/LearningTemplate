#include "pch.h"

#include "Engine/UI/Style/WidgetStyle.h"

#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct WidgetStyleInternal
        {
            using Table = UiStyleFieldTable;

            /** @brief 칸 표 — `UiStyleField` 순서, `WidgetStyle` 선언 순서와 같다. */
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
            static_assert( sizeof( kArrEntry ) / sizeof( kArrEntry[0] ) == static_cast<size_t>( UiStyleField::Count ), "UiStyleField and the table differ" );

            static bool isSameFont( const FontSpec& lhs, const FontSpec& rhs ) { return lhs._family == rhs._family && lhs._weight == rhs._weight && lhs._slant == rhs._slant; }

            /** @brief 칸 하나의 값이 같은가입니다. */
            static bool isSameValue( const WidgetStyle& lhs, const WidgetStyle& rhs, UiStyleField field )
            {
                switch ( field )
                {
                    case UiStyleField::BackgroundColor:
                        return lhs._backgroundColor == rhs._backgroundColor;
                    case UiStyleField::CornerRadius:
                        return lhs._cornerRadius == rhs._cornerRadius;
                    case UiStyleField::BorderWidth:
                        return lhs._borderWidth == rhs._borderWidth;
                    case UiStyleField::BorderColor:
                        return lhs._borderColor == rhs._borderColor;
                    case UiStyleField::ShadowColor:
                        return lhs._shadowColor == rhs._shadowColor;
                    case UiStyleField::ShadowOffset:
                        return lhs._shadowOffset == rhs._shadowOffset;
                    case UiStyleField::ShadowBlur:
                        return lhs._shadowBlur == rhs._shadowBlur;
                    case UiStyleField::Padding:
                        return lhs._padding == rhs._padding;
                    case UiStyleField::Font:
                        return isSameFont( lhs._font, rhs._font );
                    case UiStyleField::FontSize:
                        return lhs._fontSize == rhs._fontSize;
                    case UiStyleField::TextColor:
                        return lhs._textColor == rhs._textColor;
                    case UiStyleField::TextOutlineColor:
                        return lhs._textOutlineColor == rhs._textOutlineColor;
                    case UiStyleField::TextOutlineWidth:
                        return lhs._textOutlineWidth == rhs._textOutlineWidth;
                    case UiStyleField::FocusRingColor:
                        return lhs._focusRingColor == rhs._focusRingColor;
                    case UiStyleField::Opacity:
                        return lhs._opacity == rhs._opacity;
                    case UiStyleField::Transition:
                        return lhs._transition == rhs._transition;
                    case UiStyleField::Count:
                        return true;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const UiStyleFieldTable::Entry& UiStyleFieldTable::getEntry( UiStyleField field )
    {
        return WidgetStyleInternal::kArrEntry[static_cast<uint32>( field )];
    }

    bool UiStyleFieldTable::tryFindField( string_view name, UiStyleField& outField )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( UiStyleField::Count ); ++index )
        {
            if ( StringUtil::equals( name, string_view( WidgetStyleInternal::kArrEntry[index]._pName ), true ) )
            {
                outField = static_cast<UiStyleField>( index );
                return true;
            }
        }
        return false;
    }

    uint32 UiComputedStyle::computeChangedFields( const UiComputedStyle* pOld, const UiComputedStyle* pNew )
    {
        if ( pOld == pNew )
            return 0;
        static const UiComputedStyle s_empty{};
        const UiComputedStyle&       oldStyle = pOld != nullptr ? *pOld : s_empty;
        const UiComputedStyle&       newStyle = pNew != nullptr ? *pNew : s_empty;
        uint32                       changed  = oldStyle._setMask ^ newStyle._setMask;
        const uint32                 both     = oldStyle._setMask & newStyle._setMask;
        for ( uint32 index = 0; index < static_cast<uint32>( UiStyleField::Count ); ++index )
        {
            const UiStyleField field = static_cast<UiStyleField>( index );
            const uint32       bit   = UiStyleFieldTable::makeBit( field );
            if ( ( both & bit ) != 0 && WidgetStyleInternal::isSameValue( oldStyle._value, newStyle._value, field ) == false )
                changed |= bit;
        }
        return changed;
    }
} // namespace sw
