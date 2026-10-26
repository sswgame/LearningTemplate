#include "pch.h"

#include "Engine/UI/Style/WidgetStyle.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    namespace
    {
        /** @brief 멤버 형식의 float32 성분 수입니다. 수치가 아닌 형식(폰트 · 글)은 0 입니다. */
        template <typename T>
        constexpr uint32 kWidgetStyleComponentCount = 0;
        template <>
        constexpr uint32 kWidgetStyleComponentCount<float32> = 1;
        template <>
        constexpr uint32 kWidgetStyleComponentCount<float2> = 2;
        template <>
        constexpr uint32 kWidgetStyleComponentCount<float4> = 4;
        static_assert( sizeof( float2 ) == 2 * sizeof( float32 ) && sizeof( float4 ) == 4 * sizeof( float32 ), "vector members are read as contiguous float32 components" );

        struct WidgetStyleInternal
        {
            using Table = UIStyleFieldTable;

            /** @brief 서술자 표의 한 줄입니다. 수치 필드는 `WidgetStyle` 안의 오프셋과 성분 수로 읽고 쓰고 견줍니다(성분 수 0 은 수치가 아닌 필드). */
            struct FieldRow
            {
                UIStyleField _field{ UIStyleField::Count };
                uint32       _flags{ 0 };
                uint32       _offset{ 0 };
                uint32       _componentCount{ 0 };
            };

// 오프셋과 성분 수는 필드 → 멤버 표(`UIStyleFieldMember`)에서 나온다. 이 줄은 필드 이름과 플래그만 적는다.
#define SW_UI_STYLE_FIELD_ROW( field, flags )                                                                \
    FieldRow                                                                                                 \
    {                                                                                                        \
        UIStyleField::field, flags, static_cast<uint32>( UIStyleFieldMember<UIStyleField::field>::kOffset ), \
            kWidgetStyleComponentCount<UIStyleFieldMember<UIStyleField::field>::Type>                        \
    }

            /** @brief 필드마다 한 줄입니다. `UIStyleField` 순서(`WidgetStyle` PROPERTY 선언 순서)이며 아래 static_assert 가 줄마다 번호를 대어 봅니다. */
            static constexpr FieldRow kArrRow[] = {
                SW_UI_STYLE_FIELD_ROW( BackgroundColor, 0 ),
                SW_UI_STYLE_FIELD_ROW( CornerRadius, 0 ),
                SW_UI_STYLE_FIELD_ROW( BorderWidth, 0 ),
                SW_UI_STYLE_FIELD_ROW( BorderColor, 0 ),
                SW_UI_STYLE_FIELD_ROW( ShadowColor, 0 ),
                SW_UI_STYLE_FIELD_ROW( ShadowOffset, 0 ),
                SW_UI_STYLE_FIELD_ROW( ShadowBlur, 0 ),
                SW_UI_STYLE_FIELD_ROW( Padding, Table::kAffectsLayout ),
                SW_UI_STYLE_FIELD_ROW( Font, Table::kAffectsLayout | Table::kInherited ),
                SW_UI_STYLE_FIELD_ROW( FontSize, Table::kAffectsLayout | Table::kInherited ),
                SW_UI_STYLE_FIELD_ROW( TextColor, Table::kInherited ),
                SW_UI_STYLE_FIELD_ROW( TextOutlineColor, Table::kInherited ),
                SW_UI_STYLE_FIELD_ROW( TextOutlineWidth, Table::kInherited ),
                SW_UI_STYLE_FIELD_ROW( FocusRingColor, 0 ),
                SW_UI_STYLE_FIELD_ROW( Opacity, Table::kAffectsSubtree ),
                SW_UI_STYLE_FIELD_ROW( Transition, 0 ),
            };
#undef SW_UI_STYLE_FIELD_ROW
            static_assert( SW_COUNT_OF( kArrRow ) == static_cast<size_t>( UIStyleField::Count ), "UIStyleField and the descriptor table differ" );

            /** @brief 수치가 아닌 줄 수입니다. `isSameValue` 가 따로 견주는 필드(Font · Transition)와 같은 수여야 합니다(아래 static_assert). */
            static constexpr uint32 countNonNumericRows()
            {
                uint32 count = 0;
                for ( const FieldRow& row : kArrRow )
                {
                    count += row._componentCount == 0 ? 1u : 0u;
                }
                return count;
            }

            /** @brief 줄마다 자리 번호가 그 줄의 필드 번호와 같으면 true 입니다(줄 순서가 어긋나면 컴파일이 멈춘다). */
            static constexpr bool isRowOrderMatching()
            {
                for ( uint32 index = 0; index < SW_COUNT_OF( kArrRow ); ++index )
                {
                    if ( static_cast<uint32>( kArrRow[index]._field ) != index )
                        return false;
                }
                return true;
            }

            static const FieldRow& getRow( UIStyleField field ) { return kArrRow[static_cast<uint32>( field )]; }

            static bool isSameFont( const FontSpec& lhs, const FontSpec& rhs ) { return lhs._family == rhs._family && lhs._weight == rhs._weight && lhs._slant == rhs._slant; }

            /** @brief 필드 하나의 값이 같으면 true 입니다. 수치 필드는 표의 성분을 하나씩 견줍니다(`float4 ==` 와 같은 성분별 `==`). */
            static bool isSameValue( const WidgetStyle& lhs, const WidgetStyle& rhs, UIStyleField field )
            {
                const uint32 componentCount = getRow( field )._componentCount;
                if ( componentCount > 0 )
                {
                    float32 arrLeft[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                    float32 arrRight[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                    UIStyleFieldTable::readComponents( lhs, field, arrLeft );
                    UIStyleFieldTable::readComponents( rhs, field, arrRight );
                    for ( uint32 component = 0; component < componentCount; ++component )
                    {
                        if ( arrLeft[component] != arrRight[component] )
                            return false;
                    }
                    return true;
                }
                if ( field == UIStyleField::Font )
                    return isSameFont( lhs._font, rhs._font );
                return lhs._transition == rhs._transition; // 수치가 아닌 나머지 하나(아래 static_assert 가 지킨다)
            }
        };
        // 클래스 본문 안에서는 멤버 constexpr 함수를 상수식에 못 쓰므로 정의가 끝난 뒤에 확인한다.
        static_assert( WidgetStyleInternal::kArrRow[static_cast<size_t>( UIStyleField::Font )]._componentCount == 0 &&
                           WidgetStyleInternal::kArrRow[static_cast<size_t>( UIStyleField::Transition )]._componentCount == 0 && WidgetStyleInternal::countNonNumericRows() == 2,
                       "Font and Transition must be the only non-numeric style fields; a new one needs its own comparison in isSameValue" );
        static_assert( WidgetStyleInternal::isRowOrderMatching(), "descriptor rows must follow the UIStyleField order" );
    } // namespace
} // namespace sw

namespace sw
{
    uint32 UIStyleFieldTable::getFlags( UIStyleField field )
    {
        const uint32 index = static_cast<uint32>( field );
        return index < static_cast<uint32>( UIStyleField::Count ) ? WidgetStyleInternal::kArrRow[index]._flags : 0u;
    }

    uint32 UIStyleFieldTable::getComponentCount( UIStyleField field )
    {
        const uint32 index = static_cast<uint32>( field );
        return index < static_cast<uint32>( UIStyleField::Count ) ? WidgetStyleInternal::kArrRow[index]._componentCount : 0u;
    }

    void UIStyleFieldTable::readComponents( const WidgetStyle& style, UIStyleField field, float32 ( &outArr )[4] )
    {
        const uint32 componentCount = getComponentCount( field );
        if ( componentCount == 0 )
            return;
        const uint8* pBase = reinterpret_cast<const uint8*>( &style );
        std::memcpy( outArr, pBase + WidgetStyleInternal::getRow( field )._offset, componentCount * sizeof( float32 ) );
    }

    void UIStyleFieldTable::writeComponents( WidgetStyle& style, UIStyleField field, const float32 ( &arr )[4] )
    {
        const uint32 componentCount = getComponentCount( field );
        if ( componentCount == 0 )
            return;
        uint8* pBase = reinterpret_cast<uint8*>( &style );
        std::memcpy( pBase + WidgetStyleInternal::getRow( field )._offset, arr, componentCount * sizeof( float32 ) );
    }

    void UIStyleFieldTable::copyField( const WidgetStyle& source, UIStyleField field, WidgetStyle& outTarget )
    {
        if ( getComponentCount( field ) > 0 )
        {
            float32 arrComponent[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            readComponents( source, field, arrComponent );
            writeComponents( outTarget, field, arrComponent );
        }
        else if ( field == UIStyleField::Font )
        {
            outTarget._font = source._font;
        }
        else if ( field == UIStyleField::Transition )
        {
            outTarget._transition = source._transition; // 수치가 아닌 필드는 이 둘뿐이다(WidgetStyleInternal 의 static_assert)
        }
    }

    bool UIStyleFieldTable::tryFindField( string_view name, UIStyleField& outField )
    {
        const vector<PropertyInfo>& listProperty = WidgetStyle::StaticType()->getPropertiesWithBase();
        const uint32                fieldCount   = MathUtil::min( static_cast<uint32>( listProperty.size() ), static_cast<uint32>( UIStyleField::Count ) );
        for ( uint32 index = 0; index < fieldCount; ++index )
        {
            if ( name == listProperty[index]._name.view() )
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
