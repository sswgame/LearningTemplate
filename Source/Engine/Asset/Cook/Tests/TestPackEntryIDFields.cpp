// PackEntryIDPropertiesTest — 쓰는 것만 담는 쿠킹의 항목 id 칸 표(`PackConfig::_cookRules._listEntryIDField`)를 저장소의 데이터와 대조한다(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    struct PackEntryIDPropertiesTestUtil
    {
        static constexpr const utf8* kExemptionPath = "Config/Dev/EntryIDFieldExemptions.json"; ///< 값이 우연히 항목 id 와 같은 칸(이유와 함께)

        static constexpr string_view kIDSeparators = ",; \t\r\n";

        /** @brief 저장소 데이터의 모든 항목 id 와 카탈로그 파일입니다. */
        struct Catalogs
        {
            unordered_set<string> _setID;
        };

        /** @brief 쿠킹하지 않거나 항목을 부르지 않는 자리입니다 — 원본 폴더 · 시나리오(자동화 구성은 통째로 담는다) · 에디터 전용 도메인. */
        static bool isSkipped( string_view relativePath )
        {
            return StringUtil::contains( relativePath, "_raw/" ) || StringUtil::contains( relativePath, "/automation/" ) || StringUtil::startsWith( relativePath, "editor/" );
        }

        static const PackEntryCatalog* findRule( const PackCookRules& rules, string_view relativePath )
        {
            for ( const PackEntryCatalog& rule : rules._listEntryCatalog )
            {
                if ( StringUtil::endsWith( relativePath, string( "/" ) + rule._path ) )
                    return &rule;
            }
            return nullptr;
        }

        /** @brief 값의 낱말 중 항목 id 가 있으면 첫 낱말, 없으면 빈 글입니다. */
        static string findIDToken( string_view value, const unordered_set<string>& setID )
        {
            size_t cursor = 0;
            while ( cursor < value.size() )
            {
                const size_t start = value.find_first_not_of( kIDSeparators, cursor );
                if ( start == string_view::npos )
                    break;
                const size_t      end   = value.find_first_of( kIDSeparators, start );
                const string_view token = value.substr( start, end == string_view::npos ? string_view::npos : end - start );
                if ( setID.contains( string( token ) ) )
                    return string( token );
                cursor = end == string_view::npos ? value.size() : end;
            }
            return string{};
        }

        /** @brief 표에도 예외 표에도 없는 칸이 항목 id 값을 들면 실패로 알립니다. */
        struct Scan
        {
            const PackCookRules&         _rules;
            const unordered_set<string>& _setID;
            unordered_set<string>        _setAllowed;
            string                       _relativePath;
            const PackEntryCatalog*      _pRule;

            void checkValue( string_view field, string_view value ) const
            {
                if ( _setAllowed.contains( string( field ) ) )
                    return;
                const string token = findIDToken( value, _setID );
                SW_EXPECT_TRUE_MSG( token.empty(), _relativePath + ": field '" + string( field ) + "' holds catalog entry id '" + token +
                                                       "' but is not in _listEntryIDField (Config/Engine/PackConfig.json) - the cook would drop that entry's assets. If it is not a reference, add it to " + kExemptionPath );
            }

            void visit( const XMLNode& node, string_view parentName, bool bEntry ) const
            {
                for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    if ( bEntry && StringUtil::equals( attribute.getName(), string_view( _pRule->_idAttribute ) ) )
                        continue;
                    checkValue( attribute.getName(), attribute.getValue() );
                }
                const string_view name = node.getName();
                if ( StringUtil::isNullOrEmpty( node.getText() ) == false )
                    checkValue( name == "item" ? parentName : name, node.getText() ); // 목록 원소(`<_listX><item>id</item></_listX>`)의 칸은 목록이다
            }

            void walk( const XMLNode& node, string_view parentName, bool bEntry, bool bRoot ) const
            {
                visit( node, parentName, bEntry );
                for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( StringUtil::isNullOrEmpty( child.getName() ) )
                        continue;
                    const bool bChildEntry = bRoot && _pRule != nullptr && StringUtil::equals( child.getName(), string_view( _pRule->_entryElement ) );
                    walk( child, node.getName(), bChildEntry, false );
                }
            }
        };
    };
    using Util = PackEntryIDPropertiesTestUtil;
} // namespace

/**
 * @brief [PackEntryIDPropertiesTest] 항목 id 를 값으로 드는 칸은 모두 표에 있다 — 표에 없는 칸으로만 불리는 항목의 에셋은 쿠킹이 통과한 채 배포 팩에서 빠진다
 * @details 게이트를 두는 까닭: id 칸은 경로가 아니라 이름이고 손 형식 · 리플렉션 형식에 흩어져 있어 형식으로 표시할 수 없다(`AssetRef` 는 파일을 가리킨다). 값에서 거꾸로 찾는다.
 *          표에도 예외 표에도 없는 칸이 id 와 같은 값을 들면 진다. 카탈로그 줄이 항목을 하나도 못 찾으면 고르기가 조용히 비므로 그것도 진다. 언리얼은 Primary Asset 규칙이 같은 자리다(기억에 의존).
 */
SW_TEST_CASE( PackEntryIDPropertiesTest, EveryFieldHoldingAnEntryIDIsListed )
{
    PackConfig config;
    SW_REQUIRE_TRUE( ConfigManager::readConfigFile( config, FileUtil::joinPath( ProcessRoots::get()._project.str(), PackConfig::kConfigPath ) ) == ConfigReadResult::Loaded );
    const PackCookRules& rules = config._cookRules;

    string exemptionText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( ProcessRoots::get()._project.str(), Util::kExemptionPath ), exemptionText ) );
    JSONDocument exemptionDoc;
    SW_REQUIRE_TRUE( exemptionDoc.parse( exemptionText, Util::kExemptionPath ) );
    const JSONValue listExempt = exemptionDoc.getRoot().get( "listExempt" );
    SW_REQUIRE_TRUE( listExempt.isArray() );

    const string   contentRoot = FileUtil::normalizeSeparators( ProcessRoots::get()._content.str() );
    vector<string> listFile;
    SW_REQUIRE_TRUE( FileUtil::collectFiles( contentRoot, "", listFile, true ) );

    // 카탈로그 줄마다 항목 id 를 모은다.
    unordered_set<string> setID;
    for ( const PackEntryCatalog& rule : rules._listEntryCatalog )
    {
        uint32 entryCount = 0;
        for ( const string& file : listFile )
        {
            const string normalized = FileUtil::normalizeSeparators( file );
            if ( StringUtil::endsWith( normalized, string( "/" ) + rule._path ) == false )
                continue;
            string      text;
            XMLDocument doc;
            SW_REQUIRE_TRUE( FileUtil::readTextFile( normalized, text ) );
            SW_REQUIRE_TRUE( doc.parse( text, normalized ) );
            for ( XMLNode entry = doc.getRoot().findChild( rule._entryElement.c_str() ); entry.isValid(); entry = entry.findNextSibling( rule._entryElement.c_str() ) )
            {
                const string_view id = entry.getAttributeText( rule._idAttribute.c_str() );
                if ( id.empty() == false )
                {
                    setID.insert( string( id ) );
                    ++entryCount;
                }
            }
        }
        SW_EXPECT_TRUE_MSG( entryCount > 0u, "the entry catalog row '" + rule._kind + "' (" + rule._path + ", <" + rule._entryElement + " " + rule._idAttribute + ">) found no entry - the cook would select nothing from it" );
    }
    SW_REQUIRE_FALSE( setID.empty() );

    Util::Scan scan{ rules, setID, {}, {}, nullptr };
    scan._setAllowed.insert( "_base" ); // 항목 부모 칸은 고르기가 따로 따라간다(`kEntryBaseAttribute`)
    for ( const PackEntryIDField& field : rules._listEntryIDField )
    {
        scan._setAllowed.insert( field._field );
        SW_EXPECT_TRUE_MSG( VectorUtil::containsIf( rules._listEntryCatalog, [&field]( const PackEntryCatalog& rule )
        { return rule._kind == field._kind; } ),
                            "_listEntryIDField row '" + field._field + "' names unknown kind '" + field._kind + "'" );
    }
    for ( size_t index = 0; index < listExempt.size(); ++index )
    {
        SW_EXPECT_FALSE( listExempt.at( index ).get( "reason" ).asString().empty() ); // 예외는 이유와 함께다
        scan._setAllowed.insert( listExempt.at( index ).get( "field" ).asString() );
    }

    // 쿠킹하는 데이터의 모든 칸을 본다.
    uint32 scannedCount = 0;
    for ( const string& file : listFile )
    {
        const string normalized = FileUtil::normalizeSeparators( file );
        if ( StringUtil::endsWith( normalized, ".xml" ) == false || StringUtil::startsWith( normalized, contentRoot ) == false )
            continue;
        string_view relativePath = string_view( normalized ).substr( contentRoot.size() );
        if ( relativePath.empty() == false && relativePath.front() == '/' )
            relativePath.remove_prefix( 1 );
        if ( Util::isSkipped( relativePath ) )
            continue;
        string      text;
        XMLDocument doc;
        if ( FileUtil::readTextFile( normalized, text ) == false || doc.parse( text, normalized ) == false || doc.getRoot().isValid() == false )
            continue; // 읽지 못하는 글은 런타임 검증의 몫이다
        scan._relativePath = string( relativePath );
        scan._pRule        = Util::findRule( rules, relativePath );
        scan.walk( doc.getRoot(), {}, false, true );
        ++scannedCount;
    }
    SW_EXPECT_TRUE_MSG( scannedCount > 100u, "only " + sw::to_string( scannedCount ) + " data files were read - the content root is wrong and this test saw nothing" );
}
