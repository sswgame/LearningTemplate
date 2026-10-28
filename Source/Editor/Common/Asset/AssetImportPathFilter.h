/**
 * @file AssetImportPathFilter.h
 * @brief 임포트 규칙이 어떤 원본에 적용되는지 고르는 경로 조건(포함 · 제외 패턴과 경로)과, 한 원본에 규칙이 하나만 맞게 하는 선택기입니다. 텍스처와 모델 임포트 설정이 함께 씁니다.
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class JSONValue;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct AssetImportPathFilter
     * @brief 리소스 루트 기준 경로에 대한 포함 · 제외 조건입니다.
     * @details 패턴은 `/` 가 없으면 파일 이름에만 맞춥니다. `/` 가 있으면 경로를 마디(폴더 이름 · 파일 이름) 단위로 맞춥니다.
     *          마디 안에서 `*`(0 글자 이상) · `?`(한 글자)는 **마디를 넘지 않고**, 마디 하나가 통째로 `**` 이면 0 개 이상의 마디에 맞습니다
     *          (`**` 만 폴더를 넘는다). 대소문자는 가리지 않습니다. 경로(`include_paths` · `exclude_paths`)는 폴더 마디의 연속이며
     *          (`ui/` 는 `…/ui/…` 에 맞고 `…/guide/…` 에는 맞지 않는다) 부분 문자열이 아닙니다.
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

        /** @brief 규칙 객체의 `include_patterns` · `exclude_patterns` · `include_paths` · `exclude_paths` 중 있는 것을 읽습니다. 구분자 `\` 는 `/` 로 바꿉니다. */
        void parse( const JSONValue& jsonValue );

        /** @brief @p relativePath(리소스 루트 기준)가 조건에 맞는지 봅니다. */
        bool matchesPath( string_view relativePath ) const;

        /** @brief 네 목록이 모두 비어 무엇에나 맞는지 봅니다. */
        bool isCatchAll() const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct AssetImportRuleMatch
     * @brief 규칙 선택의 결과입니다. 오류(`_error` 가 비지 않음)와 "맞는 규칙 없음"(`_bMatched` 거짓)은 다른 결과입니다.
     * @tparam Rule `_name` 과 `_filter`(`AssetImportPathFilter`)를 가진 임포트 규칙
     */
    template <typename Rule>
    struct [[nodiscard]] AssetImportRuleMatch
    {
        Rule   _rule{};            ///< 맞는 규칙. 맞는 규칙이 없거나 오류이면 기본 규칙
        bool   _bMatched{ false }; ///< 규칙 하나가 맞았다
        string _error{};           ///< 한 원본에 규칙이 둘 이상 맞을 때의 설명(영어 로그 문장). 비어 있지 않으면 이 원본은 임포트할 수 없다

        bool hasError() const { return _error.empty() == false; }
    };

    /**
     * @class AssetImportRuleUtil
     * @brief 규칙 목록에서 원본 하나에 맞는 규칙을 **하나만** 고릅니다. 순서가 뜻을 갖는 "첫 매칭" 이 아닙니다.
     */
    class AssetImportRuleUtil
    {
    public:
        /**
         * @brief @p relativePath 에 맞는 규칙을 고릅니다.
         * @details 조건 없는 규칙(`isCatchAll`)은 겹침 검사에서 빼고, 조건 있는 규칙이 하나도 맞지 않을 때만 받는 나머지 규칙입니다.
         *          조건 있는 규칙이 둘 이상 맞으면 순서로 고르지 않고 `_error` 에 두 규칙의 이름과 경로를 적어 돌려줍니다 — 겹침은 `exclude_patterns` 로 풀어 씁니다.
         *          목록에서 순서는 결과에 영향을 주지 않습니다.
         * @param configName 오류 문장에 쓰는 설정 이름("ModelImportConfig")
         */
        template <typename Rule>
        [[nodiscard]] static AssetImportRuleMatch<Rule> select( const vector<Rule>& listRule, string_view relativePath, string_view configName )
        {
            AssetImportRuleMatch<Rule> match{};
            size_t                     firstIndex    = listRule.size();
            size_t                     catchAllIndex = listRule.size();
            for ( size_t ruleIndex = 0; ruleIndex < listRule.size(); ++ruleIndex )
            {
                const Rule& rule = listRule[ruleIndex];
                if ( rule._filter.matchesPath( relativePath ) == false )
                    continue;
                if ( rule._filter.isCatchAll() )
                {
                    if ( catchAllIndex == listRule.size() )
                        catchAllIndex = ruleIndex;
                    continue;
                }
                if ( firstIndex != listRule.size() )
                {
                    match._error = string( configName ) + ": '" + string( relativePath ) + "' matches both rule " + describe( listRule[firstIndex]._name, firstIndex ) +
                                   " and rule " + describe( rule._name, ruleIndex ) + ". Narrow one of them with exclude_patterns.";
                    return match;
                }
                firstIndex = ruleIndex;
            }

            const size_t chosenIndex = firstIndex != listRule.size() ? firstIndex : catchAllIndex;
            if ( chosenIndex != listRule.size() )
            {
                match._rule     = listRule[chosenIndex];
                match._bMatched = true;
            }
            return match;
        }

        /**
         * @brief 맨 끝이 아닌 자리에 있는 조건 없는 규칙의 인덱스입니다. 없으면 `listRule.size()` 입니다.
         * @details 조건 없는 규칙은 나머지를 받는 규칙이라 하나뿐이고 맨 끝에 있어야 합니다. 그 뒤에 규칙이 더 있으면 설정을 적은 사람이 의도한 순서와 다르게 읽히므로 로드 오류로 막습니다.
         */
        template <typename Rule>
        [[nodiscard]] static size_t findMisplacedCatchAllIndex( const vector<Rule>& listRule )
        {
            for ( size_t ruleIndex = 0; ruleIndex + 1 < listRule.size(); ++ruleIndex )
            {
                if ( listRule[ruleIndex]._filter.isCatchAll() )
                    return ruleIndex;
            }
            return listRule.size();
        }

    private:
        /** @brief 오류 문장에 쓰는 규칙 이름입니다. 이름이 없으면 목록 번호입니다. */
        static string describe( const string& name, size_t ruleIndex ) { return name.empty() ? "#" + to_string( static_cast<uint32>( ruleIndex ) ) : "'" + name + "'"; }
    };
} // namespace sw::editor
