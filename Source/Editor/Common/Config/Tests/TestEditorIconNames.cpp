// EditorIconNames — 아이콘 이름 → 코드포인트 표(`sweditoricons.glyphs.xml`)가 엄격하게 읽히고, 코드(`SW_EDITOR_ICON` · `SW_EDITOR_ICON_NAME`) · 컴포넌트 메타(`Icon=`) ·
// 컴포넌트 아이콘 표가 쓰는 이름이 모두 표에 있으며, 깨진 표(겹친 이름 · Missing 없음 · 틀린 코드포인트)는 읽지 않는다. 테마 키 검사(`EditorThemeKeys`)와 같은 자리다(nogpu).
#include "pch.h"

#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Config/EditorComponentIconTable.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief @p text 에서 `macro( "이름" )` 의 이름을 모읍니다. 매크로 정의처럼 리터럴이 오지 않는 자리는 건너뛴다. */
    void collectNameLiterals( string_view text, string_view macro, unordered_set<string>& outSetName )
    {
        for ( size_t pos = text.find( macro ); pos != string_view::npos; pos = text.find( macro, pos + 1 ) )
        {
            size_t cursor = pos + macro.size();
            while ( cursor < text.size() && text[cursor] == ' ' )
                ++cursor;
            if ( cursor >= text.size() || text[cursor] != '(' )
                continue;
            ++cursor;
            while ( cursor < text.size() && text[cursor] == ' ' )
                ++cursor;
            if ( cursor >= text.size() || text[cursor] != '"' )
                continue;
            const size_t end = text.find( '"', cursor + 1 );
            if ( end != string_view::npos )
                outSetName.insert( string( text.substr( cursor + 1, end - cursor - 1 ) ) );
        }
    }

    unordered_set<string> scanSourceIconNames()
    {
        unordered_set<string> setName;
        vector<AbsolutePath>  listFile;
        const bool            bCollected = FileUtil::collectFiles( ProcessRoots::get()._project.join( "Source" ), "", listFile, true );
        SW_EXPECT_TRUE( bCollected );
        for ( const AbsolutePath& filePath : listFile )
        {
            const bool bCode = FileUtil::hasExtension( filePath.str(), ".cpp" ) || FileUtil::hasExtension( filePath.str(), ".h" ) || FileUtil::hasExtension( filePath.str(), ".inl" );
            string     text;
            if ( bCode == false || StringUtil::contains( filePath.str(), "graphify-out" ) || FileUtil::readTextFile( filePath, text ) == false )
                continue;
            collectNameLiterals( text, "SW_EDITOR_ICON_NAME", setName );
            collectNameLiterals( text, "SW_EDITOR_ICON", setName );
        }
        return setName;
    }

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

    const unordered_set<string> setSourceName = scanSourceIconNames();
    SW_REQUIRE_TRUE( setSourceName.size() > 50 ); // 소스를 못 훑었으면 아래가 아무것도 증명하지 못한다
    for ( const string& name : setSourceName )
    {
        fixed_string<constant::buffer::kMax256> message;
        formatstring( message.data(), message.capacity(), "code names icon '%#' which the icon table does not have", name );
        SW_EXPECT_TRUE_MSG( hasIcon( table, name ), message.c_str() );
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
