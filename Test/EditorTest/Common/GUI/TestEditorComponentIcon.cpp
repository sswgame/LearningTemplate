#include "pch.h"

#include "Editor/Common/GUI/EditorComponentIcon.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// 아이콘은 리플렉션 에디터 메타(`Meta = "Icon=…"`)에서 온다 — Shipping 빌드에는 그 메타가 없고 에디터도 없으므로 메타를 보는 두 테스트도 없다.
#if !defined( SW_SHIPPING )

/**
 * @brief [EditorComponentIconTest] 파생 타입은 기반 줄보다 먼저 맞고(점광 → 점광, 기반 빛 아님), 모르는 타입은 기본 컴포넌트 아이콘이다
 */
SW_TEST_CASE( EditorComponentIconTest, DerivedTypeWinsOverBaseRow )
{
    const TypeRegistry& registry   = engine::getSubsystem<TypeRegistry>();
    const TypeInfo*     pPointType = registry.findType( hashed_string( "PointLightComponent" ) );
    const TypeInfo*     pSpotType  = registry.findType( hashed_string( "SpotLightComponent" ) );
    SW_ASSERT_TRUE( pPointType != nullptr && pSpotType != nullptr );
    SW_EXPECT_TRUE( string_view{ EditorComponentIcon::findRow( pPointType )._pGlyph } == editoricon::kLightPoint );
    SW_EXPECT_TRUE( string_view{ EditorComponentIcon::findRow( pSpotType )._pGlyph } == editoricon::kLightSpot );
    SW_EXPECT_TRUE( EditorComponentIcon::findRow( pPointType )._bBillboard );
    SW_EXPECT_TRUE( string_view{ EditorComponentIcon::findRow( nullptr )._pGlyph } == editoricon::kComponent );
}

/**
 * @brief [EditorComponentIconTest] 모든 등록 타입의 `Icon` · `IconColor` 메타가 아는 이름이다(철자가 틀리면 아이콘이 조용히 Category 로 떨어진다)
 */
SW_TEST_CASE( EditorComponentIconTest, EveryIconMetaNamesAKnownIconAndColor )
{
    const TypeRegistry& registry      = engine::getSubsystem<TypeRegistry>();
    uint32              iconTypeCount = 0;
    registry.forEachType( [&]( const TypeInfo& type )
    {
        const string* pIcon = type.findCustomMeta( "Icon" );
        if ( pIcon == nullptr )
            return;
        ++iconTypeCount;
        SW_EXPECT_TRUE_MSG( EditorComponentIcon::findGlyph( *pIcon ) != nullptr, type._name.c_str() );
        const string* pColor = type.findCustomMeta( "IconColor" );
        if ( pColor != nullptr )
            SW_EXPECT_TRUE_MSG( EditorComponentIcon::isKnownColorName( *pColor ), type._name.c_str() );
    } );
    SW_EXPECT_TRUE( iconTypeCount >= 40u ); // Engine 컴포넌트가 메타를 들고 있다 — 파서가 Meta 를 버리면 여기서 드러난다
}

#endif // !SW_SHIPPING

/**
 * @brief [EditorComponentIconTest] 글리프 글을 코드 포인트로 푼다(탐침이 숫자로 비교한다)
 */
SW_TEST_CASE( EditorComponentIconTest, DecodesGlyphCodePoint )
{
    SW_EXPECT_EQUAL( EditorComponentIcon::decodeGlyph( editoricon::kCamera ), 0xE032u );
    SW_EXPECT_EQUAL( EditorComponentIcon::decodeGlyph( editoricon::kLightDirectional ), 0xE049u );
    SW_EXPECT_EQUAL( EditorComponentIcon::decodeGlyph( "A" ), 0x41u );
    SW_EXPECT_EQUAL( EditorComponentIcon::decodeGlyph( nullptr ), 0u );
}
