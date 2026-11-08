/**
 * @file PluralRules.h
 * @brief CLDR 복수형 규칙 글(`i = 1 and v = 0 @integer 1`, `v = 0 and i % 10 = 2..4 and i % 100 != 12..14 @integer 2~4, 22~24, …`)을 읽어 범주를 고르는 규칙 묶음과 그 피연산자입니다.
 * @details 규칙은 문화권 데이터(`*.cultures.xml` 의 `<pluralRules one="…" other="…"/>` · `<ordinalRules …/>`)이고, 코드는 CLDR 문법(UTS #35 Part 3 "Language Plural Rules")만 안다 —
 *          언어를 더할 때 코드를 고치지 않는다(언리얼 · ICU 는 CLDR plurals.xml 을 그대로 쓴다).
 *          지원하는 문법: `condition = and_condition ('or' and_condition)*`, `and_condition = relation ('and' relation)*`,
 *          `relation = operand (('mod' | '%') value)? ('=' | '!=') range_list`, `range_list = (value | value'..'value) (',' …)*`.
 *          피연산자는 n · i · v · f · t · e(= c, 압축 표기의 지수)다. 숫자 서식은 압축 표기를 내지 않아 서식이 넘기는 e 는 늘 0 이다.
 *          범위(`3..10`)는 값이 정수일 때만 맞는다(CLDR: `n = 3..10` 은 3.5 에 거짓). 범주는 zero → one → two → few → many 순서로 보고, 맞는 것이 없으면 other 다.
 *
 *          **보기(`@integer` · `@decimal`)는 규칙 글의 일부다.** CLDR 글을 보기째 붙여 넣고(`other` 는 보기만), 읽은 뒤 보기마다 그 범주가 고르는지 대조한다(`verifySamples`) —
 *          손으로 고친 규칙이 CLDR plurals.xml 과 어긋나면 문화권 표가 읽히지 않는다(ICU 의 규칙 보기 검사와 같은 자리).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    enum class PluralCategory : uint8;

    /**
     * @struct PluralOperands
     * @brief CLDR 복수형 피연산자(n, i, v, f, e)입니다. t 는 f 에서 낸다(`PluralRuleSet`).
     */
    struct SW_API PluralOperands
    {
        float64 _absoluteValue{ 0.0 };      ///< n 이며 절댓값입니다.
        int64   _integerDigits{ 0 };        ///< i 이며 정수 부분입니다.
        int64   _fractionDigits{ 0 };       ///< f 이며 보이는 소수 자리(뒤의 0 포함)를 정수로 만든 값입니다.
        uint32  _visibleFractionCount{ 0 }; ///< v 이며 보이는 소수 자리 수입니다.
        uint32  _exponent{ 0 };             ///< e(= c) 이며 압축 표기의 10 의 지수입니다(`1.2c6` → 6). 숫자 서식은 압축 표기를 내지 않아 0 이다 — CLDR 보기가 채운다.

        /** @brief 정수의 피연산자입니다(v = 0). */
        static PluralOperands makeInteger( int64 value );
        /** @brief 소수 @p visibleFractionCount 자리로 보일 실수의 피연산자를 만듭니다. */
        static PluralOperands makeDecimal( float64 value, uint32 visibleFractionCount );
        /**
         * @brief CLDR 보기 글 하나(`15`, `1.50`, `1c6`, `1.0000001c6`)의 피연산자를 만듭니다. 보이는 자리를 글에서 그대로 센다(`1.50` 은 v = 2, f = 50).
         * @details 지수(`c` · `e`)는 소수점을 그만큼 오른쪽으로 옮기고 e 에 적는다(`1.1c6` → n = 1100000, v = 0, e = 6). 숫자가 아니거나 너무 크면 false 입니다.
         */
        [[nodiscard]] static bool parseSample( string_view text, PluralOperands& outOperands );
    };

    /**
     * @struct PluralRuleSet
     * @brief 범주(other 를 뺀 다섯)마다 컴파일한 규칙과 범주(other 포함)마다의 보기입니다. 규칙이 비어 있으면 모든 수가 other 입니다(일본어 · 한국어 · 중국어).
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

        Rule                   _arrRule[5]{};   ///< zero · one · two · few · many(`PluralCategory` 순서)
        vector<PluralOperands> _arrSample[6]{}; ///< 범주마다(other 포함) 규칙 글의 보기 — `verifySamples` 가 대조한다

        /**
         * @brief 범주 키워드 @p keyword(`one` … `other`)의 규칙 글을 읽어 넣습니다. `other` 는 조건 없이 보기만 받는다(나머지 범주다).
         *        모르는 키워드, 틀린 글 · 보기면 그 까닭(대상은 `범주 rule '글': 깊은 쪽 대상`)입니다.
         * @details 실패해도 다른 범주는 그대로다 — 읽는 쪽(문화권 표)이 표 전체를 거절한다.
         */
        [[nodiscard]] expected<void> compileRule( string_view keyword, string_view ruleText );
        /** @brief 범주 @p category 의 규칙 글에 적힌 보기 수입니다. */
        size_t countSamples( PluralCategory category ) const;
        /** @brief 보기마다 그 범주를 고르는지 대조합니다. 어긋난 첫 보기를 오류의 대상으로 돌려줍니다. 범주를 모두 읽은 뒤 부른다. */
        [[nodiscard]] expected<void> verifySamples() const;
        /** @brief 규칙이 하나도 없으면 true 입니다(모든 수가 other). */
        bool isEmpty() const;
        /** @brief @p operands 의 범주입니다. */
        PluralCategory select( const PluralOperands& operands ) const;
    };
} // namespace sw
