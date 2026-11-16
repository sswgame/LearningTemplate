#include "pch.h"

#include "Engine/Localization/PluralRules.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Localization/CultureInfo.h"

namespace sw
{
    namespace
    {
        struct PluralRulesInternal
        {
            /** @brief 범주 키워드입니다(`PluralCategory` 순서). 마지막 other 는 나머지라 조건 없이 보기만 받는다. */
            static constexpr const utf8* kArrKeyword[] = { "zero", "one", "two", "few", "many", "other" };
            /** @brief other 의 자리입니다(`kArrKeyword` 의 마지막). */
            static constexpr size_t kOtherIndex = 5;
            /** @brief 보기 범위(`0.0~1.5`) 하나가 펼칠 수 있는 가장 많은 수입니다 — 틀린 글이 메모리를 채우지 않게 한다. CLDR 의 가장 긴 범위는 수십 개다. */
            static constexpr int64 kMaxSampleRange = 1000;
            /** @brief 보기 목록의 생략 표시 `…`(U+2026) 의 UTF-8 입니다. */
            static constexpr string_view kEllipsis = "\xE2\x80\xA6";
            /** @brief 보기 목록의 두 종류 머리말입니다(`@integer` · `@decimal` 의 `@` 뒤). 길이가 같다. */
            static constexpr string_view kIntegerSamples = "integer";
            static constexpr string_view kDecimalSamples = "decimal";
            /** @brief 보기 글의 정수부 · 소수부가 가질 수 있는 가장 많은 자리입니다 — int64 에 든다. */
            static constexpr size_t kMaxSampleDigits = 18;
            /** @brief 보기 글의 압축 지수(`c6`) 상한입니다 — 지수만큼 붙인 0 이 `kMaxSampleDigits` 를 넘지 않게. CLDR 보기는 c6 까지 쓴다. */
            static constexpr int64 kMaxSampleExponent = 15;
            /** @brief 십진 자리 한 칸의 배율입니다. */
            static constexpr float64 kDecimalBase = 10.0;
            /** @brief 반올림에 더하는 절반입니다. */
            static constexpr float64 kRoundingHalf = 0.5;

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

                [[nodiscard]] expected<void> readRangeList( vector<PluralRuleSet::Clause::Range>& outListRange )
                {
                    do
                    {
                        PluralRuleSet::Clause::Range range;
                        if ( readNumber( range._min ) == false )
                            return unexpected( Error{ ErrorKind::InvalidArgument, "expected a number" } );
                        range._max = range._min;
                        if ( accept( ".." ) && readNumber( range._max ) == false )
                            return unexpected( Error{ ErrorKind::InvalidArgument, "expected a number after '..'" } );
                        if ( range._max < range._min )
                            return unexpected( Error{ ErrorKind::InvalidArgument, "range end is below its start" } );
                        outListRange.push_back( range );
                    } while ( accept( "," ) );
                    return {};
                }

                [[nodiscard]] expected<void> readClause( PluralRuleSet::Clause& outClause )
                {
                    skipSpace();
                    const utf8 operand  = _position < _text.size() ? _text[_position] : '\0';
                    const bool bKnown   = operand == 'n' || operand == 'i' || operand == 'v' || operand == 'f' || operand == 't' || operand == 'e' || operand == 'c';
                    const bool bOperand = bKnown && ( _position + 1 >= _text.size() || _text[_position + 1] == ' ' || _text[_position + 1] == '%' ||
                                                      _text[_position + 1] == '=' || _text[_position + 1] == '!' );
                    if ( bOperand == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "expected an operand n, i, v, f, t, e or c" } );
                    ++_position;
                    outClause._operand = operand;
                    if ( accept( "%" ) || accept( "mod" ) )
                    {
                        if ( readNumber( outClause._modulus ) == false || outClause._modulus <= 0.0 )
                            return unexpected( Error{ ErrorKind::InvalidArgument, "expected a positive modulus" } );
                    }
                    if ( accept( "!=" ) )
                        outClause._bNegated = true;
                    else if ( accept( "=" ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "expected '=' or '!='" } );
                    return readRangeList( outClause._listRange );
                }
            };

            /** @brief 규칙 글 하나(보기 `@integer` · `@decimal` 앞까지)를 읽습니다. */
            [[nodiscard]] static expected<void> readRule( string_view text, PluralRuleSet::Rule& outRule )
            {
                const size_t sampleStart = text.find( '@' );
                Cursor       cursor{ StringUtil::trim( text.substr( 0, sampleStart ) ), 0 };
                if ( cursor.isAtEnd() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "rule is empty" } );
                do
                {
                    PluralRuleSet::AndCondition condition;
                    do
                    {
                        PluralRuleSet::Clause clause;
                        if ( expected<void> read = cursor.readClause( clause ); read.has_value() == false )
                            return read;
                        condition.push_back( std::move( clause ) );
                    } while ( cursor.accept( "and" ) );
                    outRule.push_back( std::move( condition ) );
                } while ( cursor.accept( "or" ) );
                if ( cursor.isAtEnd() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "unexpected text after the rule", cursor._text.substr( cursor._position ) } );
                return {};
            }

            /** @brief 피연산자 값입니다(UTS #35: n 절댓값, i 정수부, v 보이는 소수 자리 수, f 보이는 소수 자리, t 뒤의 0 을 뗀 f, e · c 압축 표기의 지수). */
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
                    case 'e':
                    case 'c':
                        return static_cast<float64>( operands._exponent );
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

            /** @brief 정수 @p scaled 를 소수 @p fractionCount 자리 글로 씁니다(`150`, 2 → `1.50`). 보기 범위를 펼칠 때 쓴다. */
            static string formatScaled( int64 scaled, uint32 fractionCount )
            {
                string digits = to_string( scaled );
                while ( digits.size() <= fractionCount )
                {
                    digits.insert( digits.begin(), '0' );
                }
                if ( fractionCount > 0 )
                    digits.insert( digits.size() - fractionCount, 1, '.' );
                return digits;
            }

            /** @brief 보기 글(`1.50`)의 소수 자리 수와 점을 뗀 정수(150)입니다. 지수가 있거나 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool readScaled( string_view text, uint32& outFractionCount, int64& outScaled )
            {
                const size_t dot  = text.find( '.' );
                outFractionCount  = dot == string_view::npos ? 0 : static_cast<uint32>( text.size() - dot - 1 );
                const string bare = dot == string_view::npos ? string( text ) : string( text.substr( 0, dot ) ) + string( text.substr( dot + 1 ) );
                return bare.empty() == false && bare.find_first_not_of( "0123456789" ) == string::npos && StringUtil::parseInt64( bare, outScaled );
            }

            /** @brief 보기 하나(`15`, `2~16`, `0.0~1.5`, `1c6`)를 @p outListSample 에 펼쳐 넣습니다. */
            [[nodiscard]] static expected<void> readSampleItem( string_view item, vector<PluralOperands>& outListSample )
            {
                const size_t tilde = item.find( '~' );
                if ( tilde == string_view::npos )
                {
                    PluralOperands operands;
                    if ( PluralOperands::parseSample( item, operands ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "sample is not a number", item } );
                    outListSample.push_back( operands );
                    return {};
                }
                // 범위는 처음 글의 소수 자리 간격으로 센다(`0.00~0.04` 는 0.01 씩) — 두 끝의 자리 수가 같아야 한다.
                uint32     startFraction = 0;
                uint32     endFraction   = 0;
                int64      start         = 0;
                int64      end           = 0;
                const bool bRead         = readScaled( StringUtil::trim( item.substr( 0, tilde ) ), startFraction, start ) &&
                                   readScaled( StringUtil::trim( item.substr( tilde + 1 ) ), endFraction, end );
                if ( bRead == false || startFraction != endFraction || end < start || end - start >= kMaxSampleRange )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "sample range is not two numbers with the same decimals, in order and short", item } );
                for ( int64 scaled = start; scaled <= end; ++scaled )
                {
                    PluralOperands operands;
                    const bool     bParsed = PluralOperands::parseSample( formatScaled( scaled, startFraction ), operands );
                    SW_ASSERT( bParsed ); // 방금 숫자로 쓴 글이다
                    (void)bParsed;
                    outListSample.push_back( operands );
                }
                return {};
            }

            /** @brief 규칙 글의 보기 부분(`@integer 0, 2~16, … @decimal 0.0~1.5, …`)을 읽습니다. 보기가 없으면 빈 목록입니다. */
            [[nodiscard]] static expected<void> readSamples( string_view text, vector<PluralOperands>& outListSample )
            {
                size_t at = text.find( '@' );
                while ( at != string_view::npos )
                {
                    const size_t next    = text.find( '@', at + 1 );
                    string_view  section = text.substr( at + 1, next == string_view::npos ? string_view::npos : next - at - 1 );
                    const bool   bKind   = section.substr( 0, kIntegerSamples.size() ) == kIntegerSamples || section.substr( 0, kDecimalSamples.size() ) == kDecimalSamples;
                    if ( bKind == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "samples start with @integer or @decimal", section } );
                    section = section.substr( kIntegerSamples.size() );
                    while ( section.empty() == false )
                    {
                        const size_t      comma = section.find( ',' );
                        const string_view item  = StringUtil::trim( section.substr( 0, comma ) );
                        section                 = comma == string_view::npos ? string_view{} : section.substr( comma + 1 );
                        if ( item.empty() || item == kEllipsis )
                            continue;
                        const expected<void> itemRead = readSampleItem( item, outListSample );
                        if ( itemRead.has_value() == false )
                            return itemRead;
                    }
                    at = next;
                }
                return {};
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
    PluralOperands PluralOperands::makeInteger( int64 value )
    {
        PluralOperands operands;
        const int64    absoluteValue = value < 0 ? -value : value;
        operands._absoluteValue      = static_cast<float64>( absoluteValue );
        operands._integerDigits      = absoluteValue;
        return operands;
    }

    PluralOperands PluralOperands::makeDecimal( float64 value, uint32 visibleFractionCount )
    {
        PluralOperands operands;
        float64        scale = 1.0;
        for ( uint32 digit = 0; digit < visibleFractionCount; ++digit )
        {
            scale *= PluralRulesInternal::kDecimalBase;
        }
        // 화면에 보이는 자릿수로 먼저 반올림한다 — 정수부를 반올림 전 값에서 버림하면 1.999 가 "2" 로 보이는데 범주는 1 의 것(one)이었다(감사 A-18, ICU 는 서식한 결과로 고른다).
        const float64 scaled           = MathUtil::floor( ( value < 0.0 ? -value : value ) * scale + PluralRulesInternal::kRoundingHalf );
        const float64 absoluteValue    = scaled / scale;
        operands._absoluteValue        = absoluteValue;
        operands._integerDigits        = static_cast<int64>( absoluteValue );
        operands._visibleFractionCount = visibleFractionCount;
        operands._fractionDigits       = static_cast<int64>( scaled - static_cast<float64>( operands._integerDigits ) * scale + PluralRulesInternal::kRoundingHalf );
        return operands;
    }

    bool PluralOperands::parseSample( string_view text, PluralOperands& outOperands )
    {
        // 지수(`c` · `e`)는 소수점을 오른쪽으로 옮긴다 — `1.1c6` 은 1100000, `1.0000001c6` 은 1000000.1(v = 1).
        const size_t exponentAt = text.find_first_of( "ce" );
        int64        exponent   = 0;
        if ( exponentAt != string_view::npos && ( StringUtil::parseInt64( text.substr( exponentAt + 1 ), exponent ) == false || exponent < 0 || exponent > PluralRulesInternal::kMaxSampleExponent ) )
            return false;
        const string_view mantissa = text.substr( 0, exponentAt );
        const size_t      dot      = mantissa.find( '.' );
        string            whole    = string( mantissa.substr( 0, dot ) );
        string            fraction = dot == string_view::npos ? string{} : string( mantissa.substr( dot + 1 ) );
        const size_t      shift    = MathUtil::min( static_cast<size_t>( exponent ), fraction.size() );
        whole += fraction.substr( 0, shift );
        fraction.erase( 0, shift );
        whole.append( static_cast<size_t>( exponent ) - shift, '0' );
        const bool bDigits = whole.empty() == false && whole.size() <= PluralRulesInternal::kMaxSampleDigits && fraction.size() <= PluralRulesInternal::kMaxSampleDigits && whole.find_first_not_of( "0123456789" ) == string::npos &&
                             fraction.find_first_not_of( "0123456789" ) == string::npos;
        int64 integerDigits  = 0;
        int64 fractionDigits = 0;
        if ( bDigits == false || StringUtil::parseInt64( whole, integerDigits ) == false || ( fraction.empty() == false && StringUtil::parseInt64( fraction, fractionDigits ) == false ) )
            return false;
        float64 scale = 1.0;
        for ( size_t digit = 0; digit < fraction.size(); ++digit )
        {
            scale *= PluralRulesInternal::kDecimalBase;
        }
        outOperands                       = PluralOperands{};
        outOperands._integerDigits        = integerDigits;
        outOperands._fractionDigits       = fractionDigits;
        outOperands._visibleFractionCount = static_cast<uint32>( fraction.size() );
        outOperands._absoluteValue        = static_cast<float64>( integerDigits ) + static_cast<float64>( fractionDigits ) / scale;
        outOperands._exponent             = static_cast<uint32>( exponent );
        return true;
    }
} // namespace sw

namespace sw
{
    expected<void> PluralRuleSet::compileRule( string_view keyword, string_view ruleText )
    {
        using Internal = PluralRulesInternal;
        for ( size_t index = 0; index < std::size( Internal::kArrKeyword ); ++index )
        {
            if ( keyword != Internal::kArrKeyword[index] )
                continue;
            const bool             bOther      = index == Internal::kOtherIndex;
            const size_t           sampleStart = ruleText.find( '@' );
            const string_view      samples     = sampleStart == string_view::npos ? string_view{} : ruleText.substr( sampleStart );
            Rule                   rule;
            vector<PluralOperands> listSample;
            expected<void>         read{};
            if ( bOther && StringUtil::trim( ruleText.substr( 0, sampleStart ) ).empty() == false )
                read = unexpected( Error{ ErrorKind::InvalidArgument, "other is the rest and takes samples only" } );
            else if ( bOther == false )
                read = Internal::readRule( ruleText, rule );
            if ( read.has_value() )
                read = Internal::readSamples( samples, listSample );
            if ( read.has_value() == false )
            {
                // 문장은 리터럴(정적 수명)이라 그대로 옮기고, 대상 앞에 어느 범주의 글인지 붙인다.
                return unexpected( Error{ read.error().kind(), read.error().message().data(), keyword }
                                       .appendDetail( " rule '" )
                                       .appendDetail( ruleText )
                                       .appendDetail( "': " )
                                       .appendDetail( read.error().detail() ) );
            }
            if ( bOther == false )
                _arrRule[index] = std::move( rule );
            _arrSample[index] = std::move( listSample );
            return {};
        }
        return unexpected( Error{ ErrorKind::NotFound, "unknown plural category (zero, one, two, few, many, other)", keyword } );
    }

    size_t PluralRuleSet::countSamples( PluralCategory category ) const
    {
        const size_t index = static_cast<size_t>( category );
        return index < std::size( _arrSample ) ? _arrSample[index].size() : 0;
    }

    expected<void> PluralRuleSet::verifySamples() const
    {
        using Internal = PluralRulesInternal;
        for ( size_t index = 0; index < std::size( _arrSample ); ++index )
        {
            for ( const PluralOperands& sample : _arrSample[index] )
            {
                const size_t selected = static_cast<size_t>( select( sample ) );
                if ( selected == index )
                    continue;
                fixed_string<constant::buffer::kMax128> detail;
                formatstring( detail.data(), detail.capacity(), "n=%# (v=%#, e=%#) of %# selects %#", sample._absoluteValue, sample._visibleFractionCount, sample._exponent,
                              Internal::kArrKeyword[index], Internal::kArrKeyword[selected] );
                return unexpected( Error{ ErrorKind::InvalidArgument, "a sample selects another category - the rule text does not match its CLDR samples", detail.c_str() } );
            }
        }
        return {};
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
