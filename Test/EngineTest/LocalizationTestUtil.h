/**
 * @file LocalizationTestUtil.h
 * @brief 로컬라이제이션 시험들이 함께 쓰는 것 — 임시 폴더에 프로젝트(프로젝트 파일 · 원문 표 · 번역 표)를 쓰고, 엔진의 문화권 표를 읽습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"

#include "Engine/Localization/LocalizationManager.h"

namespace sw::test
{
    /**
     * @struct LocalizationTestUtil
     * @brief 프로젝트 이름은 `test`, 원문 표는 `test.strings.json`, 번역 표는 `<culture>.translation.json` 입니다.
     */
    struct LocalizationTestUtil
    {
        static constexpr const utf8* kCultureTable = "engine/localization/engine.cultures.json";

        /** @brief 프로젝트 파일을 쓰고 그 경로를 돌려줍니다. @p culturesJson 은 `[ "ko", "ja" ]` 같은 JSON 배열 글입니다. */
        static string writeProject( const string& folder, string_view sourceCulture, string_view culturesJson, string_view extraFields = {} )
        {
            (void)FileUtil::ensureDirectoryExists( folder );
            const string projectPath = FileUtil::joinPath( folder, "test.locproject.json" );
            string       text        = "{ \"name\": \"test\", \"sourceCulture\": \"" + string( sourceCulture ) + "\", \"cultures\": " + string( culturesJson ) +
                          ", \"stringTables\": [ \"test.strings.json\" ]";
            if ( extraFields.empty() == false )
                text += ", " + string( extraFields );
            text += " }";
            (void)FileUtil::writeTextFile( projectPath, text ); // 시험 준비 — 실패는 writeTextFile 이 오류로 남기고 뒤의 읽기 단언이 드러낸다
            return projectPath;
        }

        /** @brief 원문 표를 씁니다. @p entriesJson 은 `"Key": { "source": "…" }, …` 처럼 entries 객체의 안쪽입니다. */
        static void writeSourceTable( const string& folder, string_view culture, string_view entriesJson )
        {
            (void)FileUtil::writeTextFile( FileUtil::joinPath( folder, "test.strings.json" ), // 시험 준비 — 실패는 writeTextFile 이 오류로 남기고 뒤의 읽기 단언이 드러낸다
                                           "{ \"culture\": \"" + string( culture ) + "\", \"entries\": { " + string( entriesJson ) + " } }" );
        }

        /** @brief 번역 표를 씁니다. @p entriesJson 은 `"Key": { "text": "…" }, …` 입니다. */
        static void writeTranslation( const string& folder, string_view culture, string_view entriesJson )
        {
            // 시험 준비 — 실패는 writeTextFile 이 오류로 남기고 뒤의 읽기 단언이 드러낸다
            (void)FileUtil::writeTextFile( FileUtil::joinPath( folder, string( culture ) + ".translation.json" ),
                                           "{ \"culture\": \"" + string( culture ) + "\", \"entries\": { " + string( entriesJson ) + " } }" );
        }

        /** @brief 엔진 문화권 표를 읽은 매니저를 준비합니다. */
        [[nodiscard]] static bool loadEngineCultures( LocalizationManager& localization ) { return localization.loadCultureTable( kCultureTable ); }
    };
} // namespace sw::test
