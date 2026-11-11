/**
 * @file LocalizationTestUtil.h
 * @brief 현지화 테스트가 함께 쓰는 것 — 폴더에 프로젝트(프로젝트 파일 · 원문 표 · 번역 표)를 쓰고, 엔진의 문화권 표를 읽습니다.
 * @details 테스트는 내용을 JSON 모양의 짧은 글(`"Key": { "source": "…" }, …`)로 적고, 이 도우미가 엔진의 XML 쓰기(`SourceStringTable::saveToFile` 등)로 파일을 만듭니다.
 *          JSON 은 테스트가 내용을 적는 표기일 뿐이고 파일은 늘 엔진 형식(XML)이라 읽기 경로는 실제 데이터와 같다.
 *          폴더는 `ScopedScratchDomain` 이 붙인 도메인 폴더이고, 매니저에는 그 도메인의 id 로 올린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::test
{
    /**
     * @struct LocalizationTestUtil
     * @brief 프로젝트 이름은 `test`, 원문 표는 `test.strings.xml`, 번역 표는 `<culture>.translation.xml` 입니다.
     * @details 준비가 실패하면(글이 틀리거나 쓰지 못하면) 케이스를 실패로 적는다 — 뒤의 단언이 엉뚱한 이유로 지지 않게.
     */
    struct LocalizationTestUtil
    {
        static constexpr const utf8* kCultureTable = "engine/localization/engine.cultures.xml";

        /**
         * @brief 프로젝트 파일을 쓰고 그 디스크 경로를 돌려줍니다.
         * @param culturesJSON `[ "ko", "ja" ]` 같은 JSON 배열 글입니다.
         * @param extraProperties `"stagedCultures": [ "ko" ]` 처럼 문자열 목록 칸들의 JSON 글입니다.
         */
        static string writeProject( const string& folder, string_view sourceCulture, string_view culturesJSON, string_view extraProperties = {} )
        {
            const string projectPath = FileUtil::joinPath( folder, "test.locproject.xml" );
            string       json        = "{ \"cultures\": " + string( culturesJSON ) + ", \"stringTables\": [ \"test.strings.xml\" ]";
            if ( extraProperties.empty() == false )
                json += ", " + string( extraProperties );
            json += " }";
            JSONDocument params;
            if ( params.parse( json, "test project fields" ) == false )
            {
                reportFailure( __FILE__, __LINE__, "test project fields are not JSON: " + json );
                return projectPath;
            }
            XMLDocument   document;
            const XMLNode root = document.appendRoot( "LocalizationProject" );
            root.appendAttribute( "name", "test" );
            root.appendAttribute( "sourceCulture", sourceCulture );
            for ( const string& fieldName : params.getRoot().getMemberNames() )
            {
                const JSONValue list     = params.getRoot().get( fieldName );
                const XMLNode   listNode = root.appendChild( fieldName.c_str() );
                for ( size_t index = 0; list.isArray() && index < list.size(); ++index )
                {
                    (void)listNode.appendChild( "item", string_view{ list.at( index ).asString() } );
                }
            }
            if ( FileUtil::ensureDirectoryExists( folder ) == false || FileUtil::writeTextFile( projectPath, document.saveToString() ) == false )
                reportFailure( __FILE__, __LINE__, "could not write " + projectPath );
            return projectPath;
        }

        /** @brief 원문 표를 씁니다. @p entriesJSON 은 `"Key": { "source": "…" }, …` 처럼 entries 객체의 안쪽입니다(`source` · `context` · `comment` · `maxLength` · `origins`). */
        static void writeSourceTable( const string& folder, string_view culture, string_view entriesJSON )
        {
            JSONDocument entries;
            if ( entries.parse( "{ " + string( entriesJSON ) + " }", "test source entries" ) == false )
            {
                reportFailure( __FILE__, __LINE__, "test source entries are not JSON" );
                return;
            }
            SourceStringTable table;
            table.setCulture( culture );
            for ( const string& key : entries.getRoot().getMemberNames() )
            {
                const JSONValue  value = entries.getRoot().get( key );
                SourceTextEntry& entry = table.getOrAddEntry( key );
                entry._source          = value.get( "source" ).asString();
                entry._context         = value.get( "context" ).asString();
                entry._comment         = value.get( "comment" ).asString();
                (void)value.readOptionalInto( "maxLength", entry._maxLength ); // 없으면 제한 없음이다 — 틀린 값은 loadFromXMLText 케이스가 따로 본다
                const JSONValue origins = value.get( "origins" );
                for ( size_t index = 0; origins.isArray() && index < origins.size(); ++index )
                {
                    entry._listOrigin.push_back( origins.at( index ).asString() );
                }
            }
            save( table, FileUtil::joinPath( folder, "test.strings.xml" ) );
        }

        /** @brief 번역 표를 씁니다. @p entriesJSON 은 `"Key": { "text": "…" }, …` 입니다(`text` · `sourceHash` · `review` · `translatorComment`). */
        static void writeTranslation( const string& folder, string_view culture, string_view entriesJSON )
        {
            JSONDocument entries;
            if ( entries.parse( "{ " + string( entriesJSON ) + " }", "test translation entries" ) == false )
            {
                reportFailure( __FILE__, __LINE__, "test translation entries are not JSON" );
                return;
            }
            TranslationTable table;
            table.setCulture( culture );
            for ( const string& key : entries.getRoot().getMemberNames() )
            {
                const JSONValue   value  = entries.getRoot().get( key );
                TranslationEntry& entry  = table.getOrAddEntry( key );
                entry._text              = value.get( "text" ).asString();
                entry._translatorComment = value.get( "translatorComment" ).asString();
                (void)value.readOptionalInto( "review", entry._bReview ); // 없으면 검토 표시 없음이다
                const JSONValue hash = value.get( "sourceHash" );
                if ( hash.isValid() && SourceTextEntry::tryParseSourceHash( hash.asString(), entry._sourceHash ) == false )
                    reportFailure( __FILE__, __LINE__, "test translation has a bad sourceHash: " + hash.asString() );
            }
            save( table, FileUtil::joinPath( folder, string( culture ) + ".translation.xml" ) );
        }

        /** @brief 엔진 문화권 표를 읽은 매니저를 준비합니다. */
        [[nodiscard]] static bool loadEngineCultures( LocalizationManager& localization ) { return localization.loadCultureTable( AssetID{ kCultureTable } ); }

    private:
        template <typename TTable>
        static void save( const TTable& table, const string& path )
        {
            const optional<AbsolutePath> absPath = AbsolutePath::tryMake( path );
            const bool                   bSaved  = absPath.has_value() && FileUtil::ensureParentDirectoryExists( path ) && table.saveToFile( *absPath );
            if ( bSaved == false )
                reportFailure( __FILE__, __LINE__, "could not write " + path );
        }
    };
} // namespace sw::test
