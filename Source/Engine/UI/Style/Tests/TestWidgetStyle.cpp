#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Style/WidgetStyle.h"

using namespace sw;

namespace
{
    struct WidgetStyleTestUtil
    {
        static constexpr uint32 kFieldCount = static_cast<uint32>( UIStyleField::Count );

        /** @brief 필드 순서의 PROPERTY 철자입니다(`_` + 열거자 이름, 첫 글자 소문자). */
        static constexpr const utf8* kArrFieldName[kFieldCount] = {
            "_backgroundColor",
            "_cornerRadius",
            "_borderWidth",
            "_borderColor",
            "_shadowColor",
            "_shadowOffset",
            "_shadowBlur",
            "_padding",
            "_font",
            "_fontSize",
            "_textColor",
            "_textOutlineColor",
            "_textOutlineWidth",
            "_focusRingColor",
            "_opacity",
            "_transition",
        };

        /** @brief 필드 순서의 float32 성분 수입니다. */
        static constexpr uint32 kArrComponentCount[kFieldCount] = { 4, 4, 1, 4, 4, 2, 1, 4, 0, 1, 4, 4, 1, 4, 1, 0 };

        static UIStyleField toField( uint32 index ) { return static_cast<UIStyleField>( index ); }

        /** @brief 두 스타일을 모든 필드 비트로 정합니다(값은 같습니다). */
        static UIComputedStyle makeAllSet()
        {
            UIComputedStyle style;
            style._setMask = ( 1u << kFieldCount ) - 1u;
            return style;
        }
    };
    using Util = WidgetStyleTestUtil;
} // namespace

/** @brief [UIStyleTest] 필드 번호 · 표 · 리플렉션이 같은 순서다 — PROPERTY 이름으로 찾으면 그 번호가 나온다 */
SW_TEST_CASE( UIStyleTest, FieldTableMatchesReflection )
{
    const PropertyListView listProperty = WidgetStyle::StaticType()->getPropertiesWithBase();
    SW_REQUIRE_EQUAL( Util::kFieldCount, static_cast<uint32>( listProperty.size() ) );
    for ( uint32 index = 0; index < Util::kFieldCount; ++index )
    {
        SW_EXPECT_TRUE( listProperty[index]._name.view() == string_view( Util::kArrFieldName[index] ) );
        UIStyleField field{ UIStyleField::Count };
        SW_EXPECT_TRUE( UIStyleFieldTable::tryFindField( Util::kArrFieldName[index], field ) );
        SW_EXPECT_EQUAL( index, static_cast<uint32>( field ) );
    }
}

/** @brief [UIStyleTest] 모르는 이름은 찾지 못하고 출력을 건드리지 않는다. 철자는 정확히 맞아야 한다 */
SW_TEST_CASE( UIStyleTest, UnknownFieldNameIsNotFound )
{
    UIStyleField field{ UIStyleField::Opacity };
    SW_EXPECT_FALSE( UIStyleFieldTable::tryFindField( "_nothing", field ) );
    SW_EXPECT_FALSE( UIStyleFieldTable::tryFindField( "backgroundColor", field ) );
    SW_EXPECT_FALSE( UIStyleFieldTable::tryFindField( "_BackgroundColor", field ) );
    SW_EXPECT_FALSE( UIStyleFieldTable::tryFindField( "", field ) );
    SW_EXPECT_EQUAL( UIStyleField::Opacity, field );
}

/** @brief [UIStyleTest] 플래그 표 — 레이아웃 · 자손 · 상속이 CSS 의 뜻과 같다. 범위 밖 번호는 0 이다 */
SW_TEST_CASE( UIStyleTest, FieldFlags )
{
    using Table = UIStyleFieldTable;
    SW_EXPECT_EQUAL( 0u, Table::getFlags( UIStyleField::BackgroundColor ) );
    SW_EXPECT_EQUAL( Table::kAffectsLayout, Table::getFlags( UIStyleField::Padding ) );
    SW_EXPECT_EQUAL( Table::kAffectsLayout | Table::kInherited, Table::getFlags( UIStyleField::Font ) );
    SW_EXPECT_EQUAL( Table::kAffectsLayout | Table::kInherited, Table::getFlags( UIStyleField::FontSize ) );
    SW_EXPECT_EQUAL( Table::kInherited, Table::getFlags( UIStyleField::TextColor ) );
    SW_EXPECT_EQUAL( Table::kInherited, Table::getFlags( UIStyleField::TextOutlineColor ) );
    SW_EXPECT_EQUAL( Table::kInherited, Table::getFlags( UIStyleField::TextOutlineWidth ) );
    SW_EXPECT_EQUAL( Table::kAffectsSubtree, Table::getFlags( UIStyleField::Opacity ) );
    SW_EXPECT_EQUAL( 0u, Table::getFlags( UIStyleField::Transition ) );
    SW_EXPECT_EQUAL( 0u, Table::getFlags( UIStyleField::Count ) );
    SW_EXPECT_EQUAL( 0u, Table::getComponentCount( UIStyleField::Count ) );
}

/** @brief [UIStyleTest] 성분 수는 멤버 형식에서 나온다(float4 · float2 · float32 · 수치 아님) */
SW_TEST_CASE( UIStyleTest, ComponentCountsFollowTheMemberTypes )
{
    for ( uint32 index = 0; index < Util::kFieldCount; ++index )
    {
        SW_EXPECT_EQUAL( Util::kArrComponentCount[index], UIStyleFieldTable::getComponentCount( Util::toField( index ) ) );
    }
}

/** @brief [UIStyleTest] 수치 필드는 쓴 성분을 그대로 읽고, 그 필드의 멤버에 들어가며, 옆 필드를 건드리지 않는다 */
SW_TEST_CASE( UIStyleTest, ComponentsReadAndWriteTheirOwnMember )
{
    for ( uint32 index = 0; index < Util::kFieldCount; ++index )
    {
        const UIStyleField field          = Util::toField( index );
        const uint32       componentCount = UIStyleFieldTable::getComponentCount( field );
        if ( componentCount == 0 )
            continue;
        WidgetStyle   style;
        const float32 arrWrite[4] = { 11.0f, 22.0f, 33.0f, 44.0f };
        UIStyleFieldTable::writeComponents( style, field, arrWrite );
        float32 arrRead[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
        UIStyleFieldTable::readComponents( style, field, arrRead );
        for ( uint32 component = 0; component < 4; ++component )
        {
            // 성분 수 뒤의 칸은 건드리지 않는다.
            SW_EXPECT_NEAR( component < componentCount ? arrWrite[component] : -1.0f, arrRead[component], 0.0f );
        }
        // 다른 필드는 기본값 그대로다 — 오프셋이 어긋나면 옆 필드가 바뀐다.
        const WidgetStyle defaults;
        for ( uint32 other = 0; other < Util::kFieldCount; ++other )
        {
            const uint32 otherCount = UIStyleFieldTable::getComponentCount( Util::toField( other ) );
            if ( other == index || otherCount == 0 )
                continue;
            float32 arrActual[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            float32 arrDefault[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            UIStyleFieldTable::readComponents( style, Util::toField( other ), arrActual );
            UIStyleFieldTable::readComponents( defaults, Util::toField( other ), arrDefault );
            for ( uint32 component = 0; component < otherCount; ++component )
            {
                SW_EXPECT_NEAR( arrDefault[component], arrActual[component], 0.0f );
            }
        }
    }
}

/** @brief [UIStyleTest] 오프셋은 실제 멤버를 가리킨다 — 표로 쓴 값을 멤버 이름으로 읽는다 */
SW_TEST_CASE( UIStyleTest, ComponentsMapToNamedMembers )
{
    WidgetStyle   style;
    const float32 arrWrite[4] = { 0.25f, 0.5f, 0.75f, 1.5f };
    UIStyleFieldTable::writeComponents( style, UIStyleField::ShadowOffset, arrWrite );
    SW_EXPECT_NEAR( 0.25f, style._shadowOffset._x, 0.0f );
    SW_EXPECT_NEAR( 0.5f, style._shadowOffset._y, 0.0f );
    UIStyleFieldTable::writeComponents( style, UIStyleField::Opacity, arrWrite );
    SW_EXPECT_NEAR( 0.25f, style._opacity, 0.0f );
    UIStyleFieldTable::writeComponents( style, UIStyleField::FocusRingColor, arrWrite );
    SW_EXPECT_NEAR( 0.25f, style._focusRingColor._x, 0.0f );
    SW_EXPECT_NEAR( 1.5f, style._focusRingColor._w, 0.0f );
}

/** @brief [UIStyleTest] 수치가 아닌 필드는 성분 읽기 · 쓰기가 아무것도 하지 않는다 */
SW_TEST_CASE( UIStyleTest, NonNumericFieldsIgnoreComponents )
{
    WidgetStyle style;
    style._transition         = "Opacity 1 Linear";
    style._font._family       = "Roboto";
    const float32 arrWrite[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    UIStyleFieldTable::writeComponents( style, UIStyleField::Transition, arrWrite );
    UIStyleFieldTable::writeComponents( style, UIStyleField::Font, arrWrite );
    SW_EXPECT_TRUE( style._transition == "Opacity 1 Linear" );
    SW_EXPECT_TRUE( style._font._family == "Roboto" );

    float32 arrRead[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
    UIStyleFieldTable::readComponents( style, UIStyleField::Transition, arrRead );
    UIStyleFieldTable::readComponents( style, UIStyleField::Font, arrRead );
    SW_EXPECT_NEAR( -1.0f, arrRead[0], 0.0f );
}

/** @brief [UIStyleTest] 필드 복사는 그 필드만 옮긴다 — 수치 · 폰트 · 전환 모두 */
SW_TEST_CASE( UIStyleTest, CopyFieldMovesOnlyThatField )
{
    WidgetStyle source;
    source._fontSize     = 42.0f;
    source._padding      = float4{ 1.0f, 2.0f, 3.0f, 4.0f };
    source._font._family = "Roboto";
    source._font._weight = FontWeight::Bold;
    source._transition   = "Opacity 1 Linear";

    WidgetStyle target;
    UIStyleFieldTable::copyField( source, UIStyleField::FontSize, target );
    SW_EXPECT_NEAR( 42.0f, target._fontSize, 0.0f );
    SW_EXPECT_NEAR( 0.0f, target._padding._x, 0.0f );
    SW_EXPECT_TRUE( target._font._family.empty() );
    SW_EXPECT_TRUE( target._transition.empty() );

    UIStyleFieldTable::copyField( source, UIStyleField::Padding, target );
    SW_EXPECT_NEAR( 4.0f, target._padding._w, 0.0f );

    UIStyleFieldTable::copyField( source, UIStyleField::Font, target );
    SW_EXPECT_TRUE( target._font._family == "Roboto" );
    SW_EXPECT_TRUE( target._font._weight == FontWeight::Bold );
    SW_EXPECT_TRUE( target._transition.empty() );

    UIStyleFieldTable::copyField( source, UIStyleField::Transition, target );
    SW_EXPECT_TRUE( target._transition == "Opacity 1 Linear" );
}

/** @brief [UIStyleTest] 비교 — 같은 객체는 0, 한쪽이 없으면 다른 쪽이 정한 필드 전부다(둘 다 없으면 0) */
SW_TEST_CASE( UIStyleTest, ChangedFieldsWithMissingStyles )
{
    UIComputedStyle style;
    style._setMask = UIStyleFieldTable::makeBit( UIStyleField::Opacity ) | UIStyleFieldTable::makeBit( UIStyleField::Padding );
    SW_EXPECT_EQUAL( 0u, UIComputedStyle::computeChangedFields( &style, &style ) );
    SW_EXPECT_EQUAL( 0u, UIComputedStyle::computeChangedFields( nullptr, nullptr ) );
    SW_EXPECT_EQUAL( style._setMask, UIComputedStyle::computeChangedFields( &style, nullptr ) );
    SW_EXPECT_EQUAL( style._setMask, UIComputedStyle::computeChangedFields( nullptr, &style ) );
}

/** @brief [UIStyleTest] 비교 — 한쪽만 정한 필드는 값이 같아도 바뀐 것이고, 정하지 않은 필드의 값 차이는 보지 않는다 */
SW_TEST_CASE( UIStyleTest, ChangedFieldsFollowTheMasks )
{
    UIComputedStyle oldStyle;
    UIComputedStyle newStyle;
    oldStyle._setMask = UIStyleFieldTable::makeBit( UIStyleField::Opacity );
    newStyle._setMask = UIStyleFieldTable::makeBit( UIStyleField::FontSize );
    SW_EXPECT_EQUAL( oldStyle._setMask | newStyle._setMask, UIComputedStyle::computeChangedFields( &oldStyle, &newStyle ) );

    newStyle._setMask            = oldStyle._setMask;
    oldStyle._value._borderWidth = 3.0f; // 어느 쪽도 정하지 않은 필드다
    newStyle._value._borderWidth = 9.0f;
    SW_EXPECT_EQUAL( 0u, UIComputedStyle::computeChangedFields( &oldStyle, &newStyle ) );
}

/** @brief [UIStyleTest] 비교 — 둘 다 정한 필드는 필드마다 값으로 본다. 벡터는 성분 하나만 달라도 다르다. 폰트 · 전환은 글과 값째 본다 */
SW_TEST_CASE( UIStyleTest, ChangedFieldsCompareEachFieldByValue )
{
    const UIComputedStyle base = Util::makeAllSet();
    SW_EXPECT_EQUAL( 0u, UIComputedStyle::computeChangedFields( &base, &base ) );
    UIComputedStyle same = Util::makeAllSet();
    SW_EXPECT_EQUAL( 0u, UIComputedStyle::computeChangedFields( &base, &same ) );

    for ( uint32 index = 0; index < Util::kFieldCount; ++index )
    {
        const UIStyleField field          = Util::toField( index );
        const uint32       componentCount = UIStyleFieldTable::getComponentCount( field );
        for ( uint32 component = 0; component < componentCount; ++component )
        {
            UIComputedStyle changed = Util::makeAllSet();
            float32         arrValue[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            UIStyleFieldTable::readComponents( changed._value, field, arrValue );
            arrValue[component] += 1.0f;
            UIStyleFieldTable::writeComponents( changed._value, field, arrValue );
            SW_EXPECT_EQUAL( UIStyleFieldTable::makeBit( field ), UIComputedStyle::computeChangedFields( &base, &changed ) );
        }
    }

    const uint32    fontBit     = UIStyleFieldTable::makeBit( UIStyleField::Font );
    UIComputedStyle family      = Util::makeAllSet();
    family._value._font._family = "Roboto";
    SW_EXPECT_EQUAL( fontBit, UIComputedStyle::computeChangedFields( &base, &family ) );
    UIComputedStyle weight      = Util::makeAllSet();
    weight._value._font._weight = FontWeight::Bold;
    SW_EXPECT_EQUAL( fontBit, UIComputedStyle::computeChangedFields( &base, &weight ) );
    UIComputedStyle slant     = Util::makeAllSet();
    slant._value._font._slant = FontSlant::Italic;
    SW_EXPECT_EQUAL( fontBit, UIComputedStyle::computeChangedFields( &base, &slant ) );

    UIComputedStyle transition    = Util::makeAllSet();
    transition._value._transition = "Opacity 1 Linear";
    SW_EXPECT_EQUAL( UIStyleFieldTable::makeBit( UIStyleField::Transition ), UIComputedStyle::computeChangedFields( &base, &transition ) );
}

/** @brief [UIStyleTest] 자기 스타일은 쓴 필드만 비트가 서고 값이 함께 들어간다(값과 비트가 어긋날 길이 없다) */
SW_TEST_CASE( UIStyleTest, OwnStyleSetsValueAndBitTogether )
{
    UIOwnStyle own;
    for ( uint32 index = 0; index < Util::kFieldCount; ++index )
    {
        SW_EXPECT_FALSE( own.has( Util::toField( index ) ) );
    }
    SW_EXPECT_NEAR( 18.0f, own.getValue()._fontSize, 0.0f ); // 기본값

    own.set<UIStyleField::FontSize>( 32.0f );
    own.set<UIStyleField::Transition>( string( "Opacity 1 Linear" ) );
    SW_EXPECT_TRUE( own.has( UIStyleField::FontSize ) );
    SW_EXPECT_TRUE( own.has( UIStyleField::Transition ) );
    SW_EXPECT_FALSE( own.has( UIStyleField::Opacity ) );
    SW_EXPECT_NEAR( 32.0f, own.getValue()._fontSize, 0.0f );
    SW_EXPECT_TRUE( own.getValue()._transition == "Opacity 1 Linear" );
}
