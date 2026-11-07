/**
 * @file PluralRules.h
 * @brief CLDR 복수형 규칙 글(`i = 1 and v = 0`, `v = 0 and i % 10 = 2..4 and i % 100 != 12..14`)을 읽어 범주를 고르는 규칙 묶음입니다.
 * @details 규칙은 문화권 데이터(`*.cultures.xml` 의 `<pluralRules one="…" few="…"/>` · `<ordinalRules …/>`)이고, 코드는 CLDR 문법(UTS #35 Part 3 "Language Plural Rules")만 안다 —
 *          언어를 더할 때 코드를 고치지 않는다(언리얼 · ICU 는 CLDR plurals.xml 을 그대로 쓴다).
 *          지원하는 문법: `condition = and_condition ('or' and_condition)*`, `and_condition = relation ('and' relation)*`,
 *          `relation = operand (('mod' | '%') value)? ('=' | '!=') range_list`, `range_list = (value | value'..'value) (',' …)*`.
 *          피연산자는 n · i · v · f · t 다(e · c 는 쓰지 않는다 — 지수 표기 수를 내지 않는다). 규칙 글의 `@integer` · `@decimal` 보기는 무시한다(CLDR 글을 그대로 붙여 넣는다).
 *          범위(`3..10`)는 값이 정수일 때만 맞는다(CLDR: `n = 3..10` 은 3.5 에 거짓). 범주는 zero → one → two → few → many 순서로 보고, 맞는 것이 없으면 other 다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct PluralOperands;
    enum class PluralCategory : uint8;

    /**
     * @struct PluralRuleSet
     * @brief 범주(other 를 뺀 다섯)마다 컴파일한 규칙입니다. 비어 있으면 모든 수가 other 입니다(일본어 · 한국어 · 중국어).
     */
    struct SW_API PluralRuleSet
    {
        /** @brief 관계 하나입니다(`i % 10 = 2..4`). */
        struct Clause
        {
            struct Range
            {
                float64 _min{ 0.0 };
                float64 _max{ 0.0 };
            };

            vector<Range> _listRange{};
            float64       _modulus{ 0.0 };    ///< 0 이면 나머지를 내지 않는다
            utf8          _operand{ 'n' };    ///< n · i · v · f · t
            bool          _bNegated{ false }; ///< NOLINT(sw-bool-flag): 집합체다 — `!=`
        };

        /** @brief `and` 로 이은 관계들입니다. */
        using AndCondition = vector<Clause>;

        /** @brief 범주 하나의 규칙입니다. `or` 로 이은 조건들이고, 비어 있으면 이 범주는 고르지 않는다. */
        using Rule = vector<AndCondition>;

        Rule _arrRule[5]{}; ///< zero · one · two · few · many(`PluralCategory` 순서)

        /**
         * @brief 범주 키워드 @p keyword(`one` …)의 규칙 글을 읽어 넣습니다. 모르는 키워드(`other` 포함 — other 는 나머지다), 틀린 글이면 까닭을 @p outError 에 적고 false 입니다.
         * @details 실패해도 다른 범주는 그대로다 — 읽는 쪽(문화권 표)이 표 전체를 거절한다.
         */
        [[nodiscard]] bool compileRule( string_view keyword, string_view ruleText, string& outError );
        /** @brief 규칙이 하나도 없으면 true 입니다(모든 수가 other). */
        bool isEmpty() const;
        /** @brief @p operands 의 범주입니다. */
        PluralCategory select( const PluralOperands& operands ) const;
    };
} // namespace sw
