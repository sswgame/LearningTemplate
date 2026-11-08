#include "pch.h"

#include "Engine/Localization/PluralRules.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Localization/CultureInfo.h"

namespace sw
{
    namespace
    {
        struct PluralRulesInternal
        {
            /** @brief 규칙을 두는 범주 키워드입니다(`PluralCategory` 순서, other 는 나머지라 규칙이 없다). */
            static constexpr const utf8* kArrRuleKeyword[] = { "zero", "one", "two", "few", "many" };

            /** @brief 규칙 글을 앞에서부터 읽는 커서입니다. 낱말 · 수 · 기호를 하나씩 꺼낸다. */
            struct Cursor
            {
                string_view _text{};
                size_t      _position{ 0 };

                void skipSpace()
                {
                    while ( _position < _text.size() && ( _text[_position] == ' ' || _text[_position] == '\t' ) )
                    {
                        ++_position;
                    }
                }

                bool isAtEnd()
                {
                    skipSpace();
                    return _position >= _text.size();
                }

                /** @brief 다음이 @p token 이면 건너뛰고 true 입니다. 낱말은 뒤가 글자가 아니어야 맞는다(`and` 가 `android` 앞에서 맞지 않게). */
                bool accept( string_view token )
                {
                    skipSpace();
                    if ( _text.substr( _position, token.size() ) != token )
                        return false;
                    const size_t end     = _position + token.size();
                    const bool   bWord   = ( 'a' <= token.front() && token.front() <= 'z' );
                    const bool   bJoined = end < _text.size() && 'a' <= _text[end] && _text[end] <= 'z';
                    if ( bWord && bJoined )
                        return false;
                    _position = end;
                    return true;
                }

                /** @brief 음이 아닌 수(정수 또는 소수)를 읽습니다. 없으면 false 입니다. */
                [[nodiscard]] bool readNumber( float64& outValue )
                {
                    skipSpace();
                    const size_t start = _position;
                    while ( _position < _text.size() && ( ( '0' <= _text[_position] && _text[_position] <= '9' ) || ( _text[_position] == '.' && _text.substr( _position, 2 ) != ".." ) ) )
                    {
                        ++_position;
                    }
                    if ( _position == start )
                        return false;
                    return StringUtil::parseDouble( _text.substr( start, _position - start ), outValue );
                }

                [[nodiscard]] bool readRangeList( vector<PluralRuleSet::Clause::Range>& outListRange, string& outError )
                {
                    do
                    {
                        PluralRuleSet::Clause::Range range;
                        if ( readNumber( range._min ) == false )
                        {
                            outError = "expected a number";
                            return false;
                        }
                        range._max = range._min;
                        if ( accept( ".." ) && readNumber( range._max ) == false )
                        {
                            outError = "expected a number after '..'";
                            return false;
                        }
                        if ( range._max < range._min )
                        {
                            outError = "range end is below its start";
                            return false;
                        }
                        outListRange.push_back( range );
                    } while ( accept( "," ) );
                    return true;
                }

                [[nodiscard]] bool readClause( PluralRuleSet::Clause& outClause, string& outError )
                {
                    skipSpace();
                    const utf8 operand  = _position < _text.size() ? _text[_position] : '\0';
                    const bool bKnown   = operand == 'n' || operand == 'i' || operand == 'v' || operand == 'f' || operand == 't';
                    const bool bOperand = bKnown && ( _position + 1 >= _text.size() || _text[_position + 1] == ' ' || _text[_position + 1] == '%' ||
                                                      _text[_position + 1] == '=' || _text[_position + 1] == '!' );
                    if ( bOperand == false )
                    {
                        outError = "expected an operand n, i, v, f or t";
                        return false;
                    }
                    ++_position;
                    outClause._operand = operand;
                    if ( accept( "%" ) || accept( "mod" ) )
                    {
                        if ( readNumber( outClause._modulus ) == false || outClause._modulus <= 0.0 )
                        {
                            outError = "expected a positive modulus";
                            return false;
                        }
                    }
                    if ( accept( "!=" ) )
                        outClause._bNegated = true;
                    else if ( accept( "=" ) == false )
                    {
                        outError = "expected '=' or '!='";
                        return false;
                    }
                    return readRangeList( outClause._listRange, outError );
                }
            };

            /** @brief 규칙 글 하나(보기 `@integer` · `@decimal` 앞까지)를 읽습니다. */
            [[nodiscard]] static bool readRule( string_view text, PluralRuleSet::Rule& outRule, string& outError )
            {
                const size_t sampleStart = text.find( '@' );
                Cursor       cursor{ StringUtil::trim( text.substr( 0, sampleStart ) ), 0 };
                if ( cursor.isAtEnd() )
                {
                    outError = "rule is empty";
                    return false;
                }
                do
                {
                    PluralRuleSet::AndCondition condition;
                    do
                    {
                        PluralRuleSet::Clause clause;
                        if ( cursor.readClause( clause, outError ) == false )
                            return false;
                        condition.push_back( std::move( clause ) );
                    } while ( cursor.accept( "and" ) );
                    outRule.push_back( std::move( condition ) );
                } while ( cursor.accept( "or" ) );
                if ( cursor.isAtEnd() == false )
                {
                    outError = "unexpected '" + string( cursor._text.substr( cursor._position ) ) + "'";
                    return false;
                }
                return true;
            }

            /** @brief 피연산자 값입니다(UTS #35: n 절댓값, i 정수부, v 보이는 소수 자리 수, f 보이는 소수 자리, t 뒤의 0 을 뗀 f). */
            static float64 readOperand( utf8 operand, const PluralOperands& operands )
            {
                switch ( operand )
                {
                    case 'i':
                        return static_cast<float64>( operands._integerDigits );
                    case 'v':
                        return static_cast<float64>( operands._visibleFractionCount );
                    case 'f':
                        return static_cast<float64>( operands._fractionDigits );
                    case 't':
                    {
                        int64 trimmed = operands._fractionDigits;
                        while ( trimmed != 0 && trimmed % 10 == 0 )
                        {
                            trimmed /= 10;
                        }
                        return static_cast<float64>( trimmed );
                    }
                    default:
                        return operands._absoluteValue;
                }
            }

            static bool matches( const PluralRuleSet::Clause& relation, const PluralOperands& operands )
            {
                float64 value = readOperand( relation._operand, operands );
                if ( relation._modulus > 0.0 )
                    value = MathUtil::fmod( value, relation._modulus );
                const bool bInteger = MathUtil::floor( value ) == value;
                bool       bInList  = false;
                for ( const PluralRuleSet::Clause::Range& range : relation._listRange )
                {
                    // 범위는 정수만 받는다 — `n = 3..10` 은 3.5 에 거짓. 한 값은 같은지만 본다(`n = 1` 은 1.0 에 참).
                    const bool bInRange = range._min == range._max ? value == range._min : bInteger && range._min <= value && value <= range._max;
                    bInList             = bInList || bInRange;
                }
                return bInList != relation._bNegated;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PluralRuleSet::compileRule( string_view keyword, string_view ruleText, string& outError )
    {
        using Internal = PluralRulesInternal;
        for ( size_t index = 0; index < std::size( Internal::kArrRuleKeyword ); ++index )
        {
            if ( keyword != Internal::kArrRuleKeyword[index] )
                continue;
            Rule rule;
            if ( Internal::readRule( ruleText, rule, outError ) == false )
            {
                outError = string( keyword ) + " rule '" + string( ruleText ) + "': " + outError;
                return false;
            }
            _arrRule[index] = std::move( rule );
            return true;
        }
        outError = "unknown plural category '" + string( keyword ) + "' (zero, one, two, few, many; other is the rest)";
        return false;
    }

    bool PluralRuleSet::isEmpty() const
    {
        for ( const Rule& rule : _arrRule )
        {
            if ( rule.empty() == false )
                return false;
        }
        return true;
    }

    PluralCategory PluralRuleSet::select( const PluralOperands& operands ) const
    {
        for ( size_t index = 0; index < std::size( _arrRule ); ++index )
        {
            for ( const AndCondition& condition : _arrRule[index] )
            {
                bool bAll = true;
                for ( const Clause& relation : condition )
                {
                    bAll = bAll && PluralRulesInternal::matches( relation, operands );
                }
                if ( bAll )
                    return static_cast<PluralCategory>( index );
            }
        }
        return PluralCategory::Other;
    }
} // namespace sw
