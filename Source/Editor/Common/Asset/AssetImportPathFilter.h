/**
 * @file AssetImportPathFilter.h
 * @brief 임포트 규칙이 어떤 원본에 적용되는지 고르는 경로 조건(포함 · 제외 패턴과 경로)입니다. 텍스처와 모델 임포트 설정이 함께 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class JSONValue;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct AssetImportPathFilter
     * @brief 리소스 루트 기준 경로에 대한 포함 · 제외 조건입니다.
     * @details 패턴은 `*` · `?` 와일드카드이고 대소문자를 가리지 않으며 파일 이름이나 전체 경로 중 하나에 맞으면 됩니다. 경로는 부분 문자열입니다.
     *          제외가 먼저이고, 포함 목록이 비어 있으면 그 검사는 통과입니다 — 네 목록이 모두 비면 무엇에나 맞습니다.
     */
    struct AssetImportPathFilter
    {
        vector<string> _listIncludePattern;
        vector<string> _listExcludePattern;
        vector<string> _listIncludePath;
        vector<string> _listExcludePath;

        AssetImportPathFilter()
            : _listIncludePattern{}
            , _listExcludePattern{}
            , _listIncludePath{}
            , _listExcludePath{}
        {
        }

        /** @brief 규칙 객체의 `include_patterns` · `exclude_patterns` · `include_paths` · `exclude_paths` 중 있는 것을 읽습니다. */
        void parse( const JSONValue& jsonValue );

        /** @brief @p relativePath(리소스 루트 기준)가 조건에 맞는지 봅니다. */
        bool matchesPath( string_view relativePath ) const;

        /** @brief 네 목록이 모두 비어 무엇에나 맞는지 봅니다. */
        bool isCatchAll() const;

        /** @brief `*` · `?` 와일드카드 패턴이 @p text 전체에 맞는지 봅니다(대소문자 무시). */
        static bool matchesWildcard( string_view pattern, string_view text );
    };
} // namespace sw::editor
