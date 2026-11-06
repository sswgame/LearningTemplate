// EditorThemeKeys — 코드가 키로 가리키는 테마 토큰(`SW_THEME_COLOR` · `SW_THEME_COLOR_KEY` · `SW_THEME_METRIC`)과 데이터가 이름으로 가리키는 색(컴포넌트 아이콘 `Component.<이름>`)이
// 모든 토큰 벌에 있고, 벌에 있는 키는 코드나 데이터 어딘가가 쓴다.
// 지역화의 gather-text 검사(코드의 `SW_LOCKEY` ↔ `*.strings.xml`)와 같은 자리다. 키가 문자열이라 컴파일러가 보지 못하는 오타 · 빠진 줄 · 남은 줄을 여기서 잡는다.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Config/EditorComponentIconTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Config/EditorThemeTokens.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ThemeKeyScan
    {
        unordered_set<string> _setColorKey;  ///< 코드의 색 매크로 호출(이름공간 리터럴, 키 리터럴) → `Ns.Key`
        unordered_set<string> _setMetricKey; ///< 코드의 수치 매크로 호출
        string                _allText;      ///< 소스 전체 — 매크로 밖에서 키 글자(`"Typography.FontSize"`)로 쓰는 곳을 찾는다
    };

    /** @brief @p text 의 @p pos 에서 공백을 건너뛰고 문자열 리터럴 하나를 읽습니다. 리터럴이 아니면 false 입니다. */
    bool readLiteral( string_view text, size_t& inoutPos, string& outValue )
    {
        while ( inoutPos < text.size() && ( text[inoutPos] == ' ' || text[inoutPos] == '\t' || text[inoutPos] == '\r' || text[inoutPos] == '\n' ) )
            ++inoutPos;
        if ( inoutPos >= text.size() || text[inoutPos] != '"' )
            return false;
        const size_t end = text.find( '"', inoutPos + 1 );
        if ( end == string_view::npos )
            return false;
        outValue = string( text.substr( inoutPos + 1, end - inoutPos - 1 ) );
        inoutPos = end + 1;
        return true;
    }

    /** @brief @p text 에서 매크로 호출(리터럴 둘)의 키를 모읍니다. 매크로 정의 · 주석처럼 리터럴 둘이 오지 않는 자리는 건너뛴다. */
    void collectMacroKeys( string_view text, string_view macro, unordered_set<string>& outSetKey )
    {
        for ( size_t pos = text.find( macro ); pos != string_view::npos; pos = text.find( macro, pos + 1 ) )
        {
            size_t cursor = pos + macro.size();
            while ( cursor < text.size() && text[cursor] == ' ' )
                ++cursor;
            if ( cursor >= text.size() || text[cursor] != '(' )
                continue;
            ++cursor;
            string ns;
            string key;
            if ( readLiteral( text, cursor, ns ) == false )
                continue;
            while ( cursor < text.size() && text[cursor] == ' ' )
                ++cursor;
            if ( cursor >= text.size() || text[cursor] != ',' )
                continue;
            ++cursor;
            if ( readLiteral( text, cursor, key ) )
                outSetKey.insert( ns + "." + key );
        }
    }

    ThemeKeyScan scanSource()
    {
        ThemeKeyScan   scan;
        vector<string> listFile;
        // SW_ASSERT 안에 두면 Release 에서 식째 빠져 파일을 하나도 모으지 않는다 — 부르고 나서 결과를 단언한다.
        const bool bCollected = FileUtil::collectFiles( FileUtil::joinPath( ProcessRoots::get()._project.str(), "Source" ), "", listFile, true );
        SW_EXPECT_TRUE( bCollected );
        for ( const string& filePath : listFile )
        {
            const bool bCode = FileUtil::hasExtension( filePath, ".cpp" ) || FileUtil::hasExtension( filePath, ".h" ) || FileUtil::hasExtension( filePath, ".inl" );
            string     text;
            if ( bCode == false || StringUtil::contains( filePath, "graphify-out" ) || FileUtil::readTextFile( filePath, text ) == false )
                continue;
            collectMacroKeys( text, "SW_THEME_COLOR_KEY", scan._setColorKey );
            collectMacroKeys( text, "SW_THEME_COLOR", scan._setColorKey );
            collectMacroKeys( text, "SW_THEME_METRIC", scan._setMetricKey );
            scan._allText += text;
        }
        // 데이터가 이름으로 드는 색: 컴포넌트 아이콘 표의 색과 컴포넌트 선언의 `IconColor=` 메타(키는 `Component.<이름>`).
        EditorComponentIconTable componentTable;
        SW_EXPECT_TRUE( componentTable.loadFromAsset( AssetID{ EditorPaths::kComponentIconTable } ) );
        scan._setColorKey.insert( EditorComponentIconTable::makeColorKey( componentTable._neutralColor ) );
        for ( const EditorComponentCategoryIcon& row : componentTable._listCategory )
            scan._setColorKey.insert( EditorComponentIconTable::makeColorKey( row._color ) );
        editor::getSubsystem<TypeRegistry>().forEachType(
            [&scan]( const TypeInfo& type )
        {
            if ( const string* pColor = type.findCustomMeta( "IconColor" ) )
                scan._setColorKey.insert( EditorComponentIconTable::makeColorKey( *pColor ) );
        } );
        return scan;
    }

    /** @brief 토큰 벌을 모두 읽습니다 — 에디터가 쓰는 길(`EditorThemeTokens` 의 폴더 찾기 · 읽기)과 같다. 프로젝트 루트로 경로를 지으면 Release 의 콘텐츠 탑재와 어긋났다. */
    vector<EditorThemeConfig> loadAllThemes()
    {
        vector<string> listName;
        EditorThemeTokens::listThemeNames( listName );
        vector<EditorThemeConfig> listTheme;
        for ( const string& name : listName )
        {
            EditorThemeConfig config{};
            string            error;
            SW_EXPECT_TRUE_MSG( EditorThemeTokens::loadTheme( name, config, error ), error.c_str() );
            listTheme.push_back( std::move( config ) );
        }
        return listTheme;
    }
} // namespace

SW_TEST_CASE( EditorThemeKeys, EveryCodeKeyIsInEveryTheme )
{
    const ThemeKeyScan              scan      = scanSource();
    const vector<EditorThemeConfig> listTheme = loadAllThemes();
    SW_REQUIRE_TRUE( listTheme.size() >= 3 );
    SW_REQUIRE_TRUE( scan._setColorKey.size() > 10 ); // 소스를 못 훑었으면 아래가 아무것도 증명하지 못한다
    for ( const EditorThemeConfig& theme : listTheme )
    {
        for ( const string& key : scan._setColorKey )
            SW_EXPECT_TRUE_MSG( theme.findColor( hashed_string( string_view( key ) ) ) != nullptr, ( theme._name + " has no _color " + key ).c_str() );
        for ( const string& key : scan._setMetricKey )
            SW_EXPECT_TRUE_MSG( theme.findMetric( hashed_string( string_view( key ) ) ) != nullptr, ( theme._name + " has no _metric " + key ).c_str() );
    }
}

SW_TEST_CASE( EditorThemeKeys, EveryThemeKeyIsUsedAndEveryThemeHasTheSameKeys )
{
    const ThemeKeyScan              scan      = scanSource();
    const vector<EditorThemeConfig> listTheme = loadAllThemes();
    SW_REQUIRE_TRUE( listTheme.empty() == false );
    for ( const EditorThemeConfig& theme : listTheme )
    {
        string mismatchKey;
        SW_EXPECT_FALSE_MSG( EditorThemeTokens::findKeyMismatch( listTheme.front(), theme, mismatchKey ), ( theme._name + " differs in key " + mismatchKey ).c_str() );
    }
    // 남은 키: 매크로로도, 키 글자로도(`"Typography.FontSize"` — 테마 서브시스템보다 먼저 읽는 곳) 쓰지 않는 토큰은 지운다.
    const auto isUsed = [&scan]( const string& key, const unordered_set<string>& setMacroKey )
    { return setMacroKey.count( key ) > 0 || scan._allText.find( "\"" + key + "\"" ) != string::npos; };
    for ( const auto& [key, color] : listTheme.front()._mapColor )
        SW_EXPECT_TRUE_MSG( isUsed( key.c_str(), scan._setColorKey ), ( string( "_color " ) + key.c_str() + " is in the theme but no code uses it" ).c_str() );
    for ( const auto& [key, value] : listTheme.front()._mapMetric )
        SW_EXPECT_TRUE_MSG( isUsed( key.c_str(), scan._setMetricKey ), ( string( "_metric " ) + key.c_str() + " is in the theme but no code uses it" ).c_str() );
}
