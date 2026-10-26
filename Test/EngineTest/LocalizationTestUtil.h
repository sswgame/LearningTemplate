/**
 * @file LocalizationTestUtil.h
 * @brief 로컬라이제이션 테스트들이 함께 쓰는 것 — 임시 폴더에 프로젝트(프로젝트 파일 · 원문 표 · 번역 표)를 쓰고, 엔진의 문화권 표를 읽습니다.
 * @details 테스트는 내용을 JSON 모양의 짧은 글(`"Key": { "source": "…" }, …`)로 적고, 이 도우미가 엔진의 XML 쓰기(`SourceStringTable::saveToFile` 등)로 파일을 만듭니다.
 *          JSON 은 테스트가 내용을 적는 표기일 뿐이고 파일 형식이 아니다 — 파일은 늘 엔진 형식(XML)이라 읽기 경로는 실제 데이터와 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"

#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::test
{
    /**
     * @struct LocalizationTestUtil
     * @brief 프로젝트 이름은 `test`, 원문 표는 `test.strings.xml`, 번역 표는 `<culture>.translation.xml` 입니다.
     */
    struct LocalizationTestUtil
    {
        static constexpr const utf8* kCultureTable = "engine/localization/engine.cultures.xml";

        /**
         * @brief 프로젝트 파일을 쓰고 그 경로를 돌려줍니다.
         * @param culturesJSON `[ "ko", "ja" ]` 같은 JSON 배열 글입니다.
         * @param extraFields `"stagedCultures": [ "ko" ], "codeRoots": [ "Code" ]` 처럼 문자열 목록 칸들의 JSON 글입니다.
         */
        static string writeProject( const string& folder, string_view sourceCulture, string_view culturesJSON, string_view extraFields = {} )
        {
            (void)FileUtil::ensureDirectoryExists( folder ); // 테스트 준비 — 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
            const string projectPath = FileUtil::joinPath( folder, "test.locproject.xml" );
            string       json        = "{ \"cultures\": " + string( culturesJSON ) + ", \"stringTables\": [ \"test.strings.xml\" ]";
            if ( extraFields.empty() == false )
                json += ", " + string( extraFields );
            json += " }";
            JSONDocument fields;
            if ( fields.parse( json, "test project fields" ) == false )
                return projectPath; // 테스트 준비 — 글이 틀리면 parse 가 오류로 남기고 뒤의 읽기 단언이 드러낸다
            XMLDocument   document;
            const XMLNode root = document.appendRoot( "LocalizationProject" );
            root.appendAttribute( "name", "test" );
            root.appendAttribute( "sourceCulture", sourceCulture );
            for ( const string& fieldName : fields.getRoot().getMemberNames() )
            {
                const JSONValue list     = fields.getRoot().get( fieldName );
                const XMLNode   listNode = root.appendChild( fieldName.c_str() );
                for ( size_t index = 0; list.isArray() && index < list.size(); ++index )
                {
                    (void)listNode.appendChild( "item", string_view{ list.at( index ).asString() } );
                }
            }
            (void)FileUtil::writeTextFile( projectPath, document.saveToString() ); // 테스트 준비 — 실패는 writeTextFile 이 오류로 남기고 뒤의 읽기 단언이 드러낸다
            return projectPath;
        }

        /** @brief 원문 표를 씁니다. @p entriesJSON 은 `"Key": { "source": "…" }, …` 처럼 entries 객체의 안쪽입니다(`source` · `context` · `comment` · `maxLength` · `origins`). */
        static void writeSourceTable( const string& folder, string_view culture, string_view entriesJSON )
        {
            JSONDocument entries;
            if ( entries.parse( "{ " + string( entriesJSON ) + " }", "test source entries" ) == false )
                return; // 테스트 준비 — 글이 틀리면 parse 가 오류로 남기고 뒤의 읽기 단언이 드러낸다
            SourceStringTable table;
            table.setCulture( culture );
            for ( const string& key : entries.getRoot().getMemberNames() )
            {
                const JSONValue  value = entries.getRoot().get( key );
                SourceTextEntry& entry = table.getOrAddEntry( key );
                entry._source          = value.get( "source" ).asString();
                entry._context         = value.get( "context" ).asString();
                entry._comment         = value.get( "comment" ).asString();
                (void)value.readOptionalInto( "maxLength", entry._maxLength ); // 없으면 항목의 초기값(제한 없음)이고, 틀린 값은 loadFromXMLText 테스트가 따로 본다
                const JSONValue origins = value.get( "origins" );
                for ( size_t index = 0; origins.isArray() && index < origins.size(); ++index )
                {
                    entry._listOrigin.push_back( origins.at( index ).asString() );
                }
            }
            (void)FileUtil::ensureDirectoryExists( folder );                            // 테스트 준비 — 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
            (void)table.saveToFile( FileUtil::joinPath( folder, "test.strings.xml" ) ); // 테스트 준비 — 실패는 오류로 남고 뒤의 읽기 단언이 드러낸다
        }

        /** @brief 번역 표를 씁니다. @p entriesJSON 은 `"Key": { "text": "…" }, …` 입니다(`text` · `sourceHash` · `review` · `translatorComment`). */
        static void writeTranslation( const string& folder, string_view culture, string_view entriesJSON )
        {
            JSONDocument entries;
            if ( entries.parse( "{ " + string( entriesJSON ) + " }", "test translation entries" ) == false )
                return; // 테스트 준비 — 글이 틀리면 parse 가 오류로 남기고 뒤의 읽기 단언이 드러낸다
            TranslationTable table;
            table.setCulture( culture );
            for ( const string& key : entries.getRoot().getMemberNames() )
            {
                const JSONValue   value  = entries.getRoot().get( key );
                TranslationEntry& entry  = table.getOrAddEntry( key );
                entry._text              = value.get( "text" ).asString();
                entry._translatorComment = value.get( "translatorComment" ).asString();
                (void)value.readOptionalInto( "review", entry._bReview ); // 없으면 검토 표시 없음이다 — 테스트 준비라 틀린 값은 뒤의 읽기 단언이 드러낸다
                const JSONValue hash = value.get( "sourceHash" );
                if ( hash.isValid() )
                    (void)LocalizationTextUtil::tryParseSourceHash( hash.asString(), entry._sourceHash ); // 틀린 해시는 0(확인 안 함)으로 남는다 — 해시 오류는 loadFromXMLText 테스트가 따로 본다
            }
            (void)FileUtil::ensureDirectoryExists( folder );                                                // 테스트 준비 — 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
            (void)table.saveToFile( FileUtil::joinPath( folder, string( culture ) + ".translation.xml" ) ); // 테스트 준비 — 실패는 오류로 남고 뒤의 읽기 단언이 드러낸다
        }

        /** @brief 엔진 문화권 표를 읽은 매니저를 준비합니다. */
        [[nodiscard]] static bool loadEngineCultures( LocalizationManager& localization ) { return localization.loadCultureTable( kCultureTable ); }
    };
} // namespace sw::test
