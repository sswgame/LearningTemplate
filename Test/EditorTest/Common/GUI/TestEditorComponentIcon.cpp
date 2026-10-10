#include "pch.h"

#include "Editor/Common/GUI/EditorComponentIcon.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorComponentIconTest] 파생 타입은 기반 줄보다 먼저 맞고(점광 → 점광, 기반 빛 아님), 모르는 타입은 기본 컴포넌트 아이콘이다
 */
SW_TEST_CASE( EditorComponentIconTest, DerivedTypeWinsOverBaseRow )
{
    const TypeRegistry& registry   = engine::getTypeRegistry();
    const TypeInfo*     pPointType = registry.findType( hashed_string( "PointLightComponent" ) );
    const TypeInfo*     pSpotType  = registry.findType( hashed_string( "SpotLightComponent" ) );
    SW_ASSERT_TRUE( pPointType != nullptr && pSpotType != nullptr );
    SW_EXPECT_TRUE( string_view{ EditorComponentIcon::findRow( pPointType )._pGlyph } == editoricon::kLightPoint );
    SW_EXPECT_TRUE( string_view{ EditorComponentIcon::findRow( pSpotType )._pGlyph } == editoricon::kLightSpot );
    SW_EXPECT_TRUE( EditorComponentIcon::findRow( pPointType )._bBillboard );
    SW_EXPECT_TRUE( string_view{ EditorComponentIcon::findRow( nullptr )._pGlyph } == editoricon::kComponent );
}

/**
 * @brief [EditorComponentIconTest] 표의 타입 이름은 모두 레지스트리에 있다(타입 이름을 바꾸면 그 줄이 조용히 죽는다)
 */
SW_TEST_CASE( EditorComponentIconTest, EveryTypeRowNamesARegisteredType )
{
    const TypeRegistry& registry = engine::getTypeRegistry();
    for ( uint32 index = 0; index < EditorComponentIcon::getTypeRowCount(); ++index )
    {
        const EditorComponentIconRow& row = EditorComponentIcon::getTypeRow( index );
        SW_EXPECT_TRUE_MSG( registry.findType( hashed_string( row._pKey ) ) != nullptr, row._pKey );
    }
}

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
