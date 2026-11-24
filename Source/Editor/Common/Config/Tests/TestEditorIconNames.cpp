// EditorIconNames — 아이콘 이름 → 코드포인트 표(`sweditoricons.glyphs.xml`)가 엄격하게 읽히고, 코드가 쓰는 이름 목록(`EditorIconNameList` — 목록 밖의 리터럴은 컴파일되지 않는다) ·
// 컴포넌트 메타(`Icon=`) · 컴포넌트 아이콘 표가 쓰는 이름이 모두 표에 있으며, 깨진 표(겹친 이름 · Missing 없음 · 틀린 코드포인트)는 읽지 않는다. 테마 키 검사(`EditorThemeKeys`)와 같은 자리다(nogpu).
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Config/EditorComponentIconTable.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorIconName.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    EditorIconGlyphTable loadEngineTable()
    {
        EditorIconGlyphTable table;
        SW_EXPECT_TRUE( table.loadFromAsset( AssetID{ EditorPaths::kIconGlyphTable } ) );
        return table;
    }

    bool hasIcon( const EditorIconGlyphTable& table, string_view name )
    {
        return table.findGlyph( hashed_string{ name } ) != nullptr;
    }
} // namespace

SW_TEST_CASE( EditorIconNames, EveryNameCodeAndDataUseIsInTheTable )
{
    const EditorIconGlyphTable table = loadEngineTable();
    SW_REQUIRE_TRUE( hasIcon( table, EditorIconGlyphTable::kMissingIconName ) );
    SW_REQUIRE_TRUE( table.getGlyphRanges().empty() == false && table.getGlyphRanges().back() == 0 );

    static_assert( EditorIconName::findIndex( "Sp" ) == EditorIconName::kCount, "a prefix of a name is not the name" );
    static_assert( EditorIconName::findIndex( "" ) == EditorIconName::kCount, "an empty literal is not a name" );
    static_assert( SW_EDITOR_ICON_NAME( "Spline" ).isEmpty() == false && EditorIconName{}.isEmpty(), "names come from the list" );
    for ( const utf8* pName : EditorIconName::List::kArrName )
    {
        fixed_string<constant::buffer::kMax256> message;
        formatstring( message.data(), message.capacity(), "EditorIconNameList lists '%#' which the icon table does not have", pName );
        SW_EXPECT_TRUE_MSG( hasIcon( table, pName ), message.c_str() );
    }

    EditorComponentIconTable componentTable;
    SW_REQUIRE_TRUE( componentTable.loadFromAsset( AssetID{ EditorPaths::kComponentIconTable } ) );
    for ( const string* pName : { &componentTable._defaultIcon, &componentTable._objectIcon } )
    {
        fixed_string<constant::buffer::kMax256> message;
        formatstring( message.data(), message.capacity(), "component icon table names '%#'", *pName );
        SW_EXPECT_TRUE_MSG( hasIcon( table, *pName ), message.c_str() );
    }
    for ( const EditorComponentCategoryIcon& row : componentTable._listCategory )
    {
        fixed_string<constant::buffer::kMax256> message;
        formatstring( message.data(), message.capacity(), "category '%#' names icon '%#'", row._category, row._icon );
        SW_EXPECT_TRUE_MSG( hasIcon( table, row._icon ), message.c_str() );
    }

    uint32 metaCount{ 0 };
    editor::getSubsystem<TypeRegistry>().forEachType(
        [&]( const TypeInfo& type )
    {
        const string* pIcon = type.findCustomMeta( "Icon" );
        if ( pIcon == nullptr )
            return;
        ++metaCount;
        fixed_string<constant::buffer::kMax256> message;
        formatstring( message.data(), message.capacity(), "%# has Meta Icon=%# which the icon table does not have", type._name.c_str(), *pIcon );
        SW_EXPECT_TRUE_MSG( hasIcon( table, *pIcon ), message.c_str() );
    } );
    SW_EXPECT_TRUE( metaCount > 5 );
}

SW_TEST_CASE( EditorIconNames, BrokenTablesAreNotRead )
{
    const auto makeTable = []( string_view firstRow, string_view secondRow )
    {
        StringBuilder<constant::buffer::kMax512> tableText;
        tableText.appendFormat( "<EditorIconGlyphTable><_listIcon>%#%#</_listIcon></EditorIconGlyphTable>", firstRow, secondRow );
        return string( tableText.view() );
    };
    const string missingRow = R"(<EditorIconGlyph _name="Missing" _codepoint="U+E060" />)";

    EditorIconGlyphTable table;
    SW_EXPECT_LOG( "EditorIcon.Duplicate" );
    SW_EXPECT_FALSE( table.loadFromXMLText( makeTable( missingRow, R"(<EditorIconGlyph _name="Save" _codepoint="U+E060" />)" ), "duplicate" ) );
    SW_EXPECT_FALSE( hasIcon( table, "Missing" ) ); // 실패한 읽기는 아무것도 남기지 않는다
    SW_EXPECT_LOG( "EditorIcon.NoMissingIcon" );
    SW_EXPECT_FALSE( table.loadFromXMLText( makeTable( R"(<EditorIconGlyph _name="Save" _codepoint="U+E003" />)", "" ), "nomissing" ) );
    SW_EXPECT_LOG( "EditorIcon.BadRow" );
    SW_EXPECT_FALSE( table.loadFromXMLText( makeTable( missingRow, R"(<EditorIconGlyph _name="Save" _codepoint="E003" />)" ), "badcode" ) );

    SW_REQUIRE_TRUE( table.loadFromXMLText( makeTable( missingRow, R"(<EditorIconGlyph _name="Save" _codepoint="U+E003" />)" ), "good" ) );
    const vector<uint16> listExpected{ 0xE003, 0xE003, 0xE060, 0xE060, 0 };
    SW_EXPECT_TRUE( table.getGlyphRanges() == listExpected );
    uint16 codepoint{ 0 };
    SW_EXPECT_FALSE( EditorIconGlyphTable::parseCodepoint( "U+e003", codepoint ) ); // 대문자만
    SW_EXPECT_FALSE( EditorIconGlyphTable::parseCodepoint( "U+10000", codepoint ) );
    SW_EXPECT_TRUE( EditorIconGlyphTable::parseCodepoint( "U+E003", codepoint ) && codepoint == 0xE003 );
}
