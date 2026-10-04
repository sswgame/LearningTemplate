#include "pch.h"

#include "Engine/Localization/CultureInfo.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "CultureInfo" );

    namespace
    {
        struct CultureInfoInternal
        {
            static PluralCategory selectNone( const PluralOperands& /*operands*/ ) { return PluralCategory::Other; }

            static PluralCategory selectEnglish( const PluralOperands& operands )
            {
                const bool bOne = operands._integerDigits == 1 && operands._visibleFractionCount == 0;
                return bOne ? PluralCategory::One : PluralCategory::Other;
            }

            static PluralCategory selectFrench( const PluralOperands& operands )
            {
                const bool bOne = operands._integerDigits == 0 || operands._integerDigits == 1;
                return bOne ? PluralCategory::One : PluralCategory::Other;
            }

            static PluralCategory selectRussian( const PluralOperands& operands )
            {
                if ( operands._visibleFractionCount != 0 )
                    return PluralCategory::Other;
                const int64 mod10  = operands._integerDigits % 10;
                const int64 mod100 = operands._integerDigits % 100;
                if ( mod10 == 1 && mod100 != 11 )
                    return PluralCategory::One;
                const bool bFew = 2 <= mod10 && mod10 <= 4 && ( 12 <= mod100 && mod100 <= 14 ) == false;
                if ( bFew )
                    return PluralCategory::Few;
                return PluralCategory::Many;
            }

            static PluralCategory selectPolish( const PluralOperands& operands )
            {
                if ( operands._visibleFractionCount != 0 )
                    return PluralCategory::Other;
                const int64 integer = operands._integerDigits;
                const int64 mod10   = integer % 10;
                const int64 mod100  = integer % 100;
                if ( integer == 1 )
                    return PluralCategory::One;
                const bool bFew = 2 <= mod10 && mod10 <= 4 && ( 12 <= mod100 && mod100 <= 14 ) == false;
                if ( bFew )
                    return PluralCategory::Few;
                return PluralCategory::Many;
            }

            static PluralCategory selectCzech( const PluralOperands& operands )
            {
                if ( operands._visibleFractionCount != 0 )
                    return PluralCategory::Many;
                if ( operands._integerDigits == 1 )
                    return PluralCategory::One;
                if ( 2 <= operands._integerDigits && operands._integerDigits <= 4 )
                    return PluralCategory::Few;
                return PluralCategory::Other;
            }

            static PluralCategory selectArabic( const PluralOperands& operands )
            {
                if ( operands._visibleFractionCount != 0 && operands._fractionDigits != 0 )
                    return PluralCategory::Other;
                const int64 integer = operands._integerDigits;
                const int64 mod100  = integer % 100;
                if ( integer == 0 )
                    return PluralCategory::Zero;
                if ( integer == 1 )
                    return PluralCategory::One;
                if ( integer == 2 )
                    return PluralCategory::Two;
                if ( 3 <= mod100 && mod100 <= 10 )
                    return PluralCategory::Few;
                if ( 11 <= mod100 && mod100 <= 99 )
                    return PluralCategory::Many;
                return PluralCategory::Other;
            }

            struct RuleRow
            {
                const utf8*    _pName;
                PluralRuleFunc _pRule;
            };

            static constexpr RuleRow kArrRule[] = {
                {   "none",    &selectNone},
                {"english", &selectEnglish},
                { "french",  &selectFrench},
                {"russian", &selectRussian},
                { "polish",  &selectPolish},
                {  "czech",   &selectCzech},
                { "arabic",  &selectArabic},
            };

            static constexpr const utf8* kArrCategoryName[] = { "zero", "one", "two", "few", "many", "other" };
            static_assert( SW_COUNT_OF( kArrCategoryName ) == static_cast<size_t>( PluralCategory::Count ), "kArrCategoryName must name every PluralCategory" );

            static constexpr const utf8* kArrCultureField[] = { "parent", "nativeName", "pluralRule", "rightToLeft", "digits", "decimalSeparator",
                                                                "groupSeparator", "percentPattern", "datePatterns", "timePatterns", "monthNames", "monthAbbreviations",
                                                                "dayPeriods", "fonts", "pseudo" };

            static bool isKnownField( string_view name )
            {
                for ( const utf8* pField : kArrCultureField )
                {
                    if ( name == pField )
                        return true;
                }
                return false;
            }

            /** @brief 2 자리 이상으로 0 을 채운 ASCII 정수입니다. */
            static string makePadded( int64 value, uint32 width )
            {
                utf8         arrBuffer[constant::kMaxBuffer32]{};
                const uint32 length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, value );
                string       text( arrBuffer, length );
                while ( text.size() < width )
                    text.insert( text.begin(), '0' );
                return text;
            }

            /** @brief 문자열 배열 칸을 읽습니다. 배열이 아니거나 원소가 문자열이 아니면 false 입니다. */
            [[nodiscard]] static bool readStringArray( const JsonValue& value, vector<string>& outList )
            {
                if ( value.isArray() == false )
                    return false;
                outList.clear();
                for ( size_t index = 0; index < value.size(); ++index )
                {
                    const JsonValue element = value.at( index );
                    if ( element.isString() == false )
                        return false;
                    outList.push_back( element.asString() );
                }
                return true;
            }

            /** @brief 패턴 묶음(`{ "short": …, "medium": … }`)을 읽습니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool readPatternGroup( const JsonValue& value, string* pShort, string* pMedium, string* pLong, string& outError )
            {
                if ( value.isObject() == false )
                {
                    outError = "patterns must be an object";
                    return false;
                }
                for ( const string& styleName : value.getMemberNames() )
                {
                    const JsonValue pattern = value.get( styleName, false );
                    string*         pTarget = nullptr;
                    if ( styleName == "short" )
                        pTarget = pShort;
                    else if ( styleName == "medium" )
                        pTarget = pMedium;
                    else if ( styleName == "long" )
                        pTarget = pLong;
                    if ( pTarget == nullptr || pattern.isString() == false )
                    {
                        outError = "unknown or non-string pattern style '" + styleName + "'";
                        return false;
                    }
                    *pTarget = pattern.asString();
                }
                return true;
            }

            /** @brief 한 문화권 객체의 칸을 @p inoutInfo 에 덮어씁니다(적힌 칸만). */
            [[nodiscard]] static bool applyFields( const JsonValue& entry, CultureInfo& inoutInfo, string& outError )
            {
                for ( const string& fieldName : entry.getMemberNames() )
                {
                    if ( isKnownField( fieldName ) == false )
                    {
                        outError = "unknown field '" + fieldName + "'";
                        return false;
                    }
                }

                const JsonValue nativeName = entry.get( "nativeName", false );
                if ( nativeName.isValid() )
                    inoutInfo._nativeName = nativeName.asString();
                const JsonValue pluralRule = entry.get( "pluralRule", false );
                if ( pluralRule.isValid() )
                {
                    const string   ruleName = pluralRule.asString();
                    PluralRuleFunc pRule    = PluralRuleUtil::findRule( ruleName );
                    if ( pRule == nullptr )
                    {
                        outError = "unknown pluralRule '" + ruleName + "'";
                        return false;
                    }
                    inoutInfo._pluralRuleName = ruleName;
                    inoutInfo._pPluralRule    = pRule;
                }
                const JsonValue rightToLeft = entry.get( "rightToLeft", false );
                if ( rightToLeft.isValid() )
                    inoutInfo._bRightToLeft = rightToLeft.asBool( false );
                const JsonValue digits = entry.get( "digits", false );
                if ( digits.isValid() )
                {
                    const string digitsName = digits.asString();
                    if ( digitsName == "latn" )
                        inoutInfo._numeralSystem = NumeralSystem::Latin;
                    else if ( digitsName == "arab" )
                        inoutInfo._numeralSystem = NumeralSystem::ArabicIndic;
                    else
                    {
                        outError = "unknown digits '" + digitsName + "' (latn, arab)";
                        return false;
                    }
                }
                const JsonValue pseudo = entry.get( "pseudo", false );
                if ( pseudo.isValid() )
                {
                    const string pseudoName = pseudo.asString();
                    if ( pseudoName == "accented" )
                        inoutInfo._pseudoMode = PseudoLocaleMode::Accented;
                    else if ( pseudoName == "mirrored" )
                        inoutInfo._pseudoMode = PseudoLocaleMode::Mirrored;
                    else
                    {
                        outError = "unknown pseudo '" + pseudoName + "' (accented, mirrored)";
                        return false;
                    }
                }

                const JsonValue decimalSeparator = entry.get( "decimalSeparator", false );
                if ( decimalSeparator.isValid() )
                    inoutInfo._decimalSeparator = decimalSeparator.asString();
                const JsonValue groupSeparator = entry.get( "groupSeparator", false );
                if ( groupSeparator.isValid() )
                    inoutInfo._groupSeparator = groupSeparator.asString();
                const JsonValue percentPattern = entry.get( "percentPattern", false );
                if ( percentPattern.isValid() )
                    inoutInfo._percentPattern = percentPattern.asString();

                const JsonValue datePatterns = entry.get( "datePatterns", false );
                if ( datePatterns.isValid() && readPatternGroup( datePatterns, &inoutInfo._dateShort, &inoutInfo._dateMedium, &inoutInfo._dateLong, outError ) == false )
                    return false;
                const JsonValue timePatterns = entry.get( "timePatterns", false );
                if ( timePatterns.isValid() && readPatternGroup( timePatterns, &inoutInfo._timeShort, &inoutInfo._timeMedium, nullptr, outError ) == false )
                    return false;

                const JsonValue monthNames = entry.get( "monthNames", false );
                if ( monthNames.isValid() && ( readStringArray( monthNames, inoutInfo._listMonthName ) == false || inoutInfo._listMonthName.size() != 12 ) )
                {
                    outError = "monthNames must be 12 strings";
                    return false;
                }
                const JsonValue monthAbbreviations = entry.get( "monthAbbreviations", false );
                const bool      bBadAbbreviations  = monthAbbreviations.isValid() &&
                                               ( readStringArray( monthAbbreviations, inoutInfo._listMonthAbbreviation ) == false || inoutInfo._listMonthAbbreviation.size() != 12 );
                if ( bBadAbbreviations )
                {
                    outError = "monthAbbreviations must be 12 strings";
                    return false;
                }
                const JsonValue dayPeriods = entry.get( "dayPeriods", false );
                if ( dayPeriods.isValid() && ( readStringArray( dayPeriods, inoutInfo._listDayPeriod ) == false || inoutInfo._listDayPeriod.size() != 2 ) )
                {
                    outError = "dayPeriods must be 2 strings";
                    return false;
                }
                const JsonValue fonts = entry.get( "fonts", false );
                if ( fonts.isValid() && readStringArray( fonts, inoutInfo._listFont ) == false )
                {
                    outError = "fonts must be an array of strings";
                    return false;
                }
                return true;
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
        const float64  absoluteValue   = value < 0.0 ? -value : value;
        operands._absoluteValue        = absoluteValue;
        operands._integerDigits        = static_cast<int64>( absoluteValue );
        operands._visibleFractionCount = visibleFractionCount;
        float64 scale                  = 1.0;
        for ( uint32 digit = 0; digit < visibleFractionCount; ++digit )
            scale *= 10.0;
        const float64 fraction   = ( absoluteValue - static_cast<float64>( operands._integerDigits ) ) * scale;
        operands._fractionDigits = static_cast<int64>( fraction + 0.5 );
        return operands;
    }

    PluralRuleFunc PluralRuleUtil::findRule( string_view ruleName )
    {
        for ( const CultureInfoInternal::RuleRow& row : CultureInfoInternal::kArrRule )
        {
            if ( ruleName == row._pName )
                return row._pRule;
        }
        return nullptr;
    }

    void PluralRuleUtil::collectRuleNames( vector<string>& outListName )
    {
        outListName.clear();
        for ( const CultureInfoInternal::RuleRow& row : CultureInfoInternal::kArrRule )
            outListName.push_back( row._pName );
    }

    const utf8* PluralRuleUtil::getCategoryName( PluralCategory category )
    {
        const size_t index = static_cast<size_t>( category );
        return index < SW_COUNT_OF( CultureInfoInternal::kArrCategoryName ) ? CultureInfoInternal::kArrCategoryName[index] : "other";
    }

    bool PluralRuleUtil::tryParseCategory( string_view keyword, PluralCategory& outCategory )
    {
        for ( size_t index = 0; index < SW_COUNT_OF( CultureInfoInternal::kArrCategoryName ); ++index )
        {
            if ( keyword == CultureInfoInternal::kArrCategoryName[index] )
            {
                outCategory = static_cast<PluralCategory>( index );
                return true;
            }
        }
        return false;
    }

    CultureInfo::CultureInfo()
        : _code{}
        , _parentCode{}
        , _nativeName{}
        , _pluralRuleName{ "none" }
        , _decimalSeparator{ "." }
        , _groupSeparator{ "," }
        , _percentPattern{ "#%" }
        , _dateShort{ "y-MM-dd" }
        , _dateMedium{ "y-MM-dd" }
        , _dateLong{ "y-MM-dd" }
        , _timeShort{ "HH:mm" }
        , _timeMedium{ "HH:mm:ss" }
        , _listMonthName{}
        , _listMonthAbbreviation{}
        , _listDayPeriod{}
        , _listFont{}
        , _pPluralRule{ PluralRuleUtil::findRule( "none" ) }
        , _numeralSystem{ NumeralSystem::Latin }
        , _pseudoMode{ PseudoLocaleMode::None }
        , _bRightToLeft{ false }
    {
    }

    PluralCategory CultureInfo::selectPlural( const PluralOperands& operands ) const
    {
        return _pPluralRule != nullptr ? _pPluralRule( operands ) : PluralCategory::Other;
    }

    void CultureInfo::appendDigits( string& inoutText, string_view asciiDigits ) const
    {
        for ( const utf8 character : asciiDigits )
        {
            const bool bDigit = '0' <= character && character <= '9';
            if ( bDigit && _numeralSystem == NumeralSystem::ArabicIndic )
                StringUtil::appendUtf8( inoutText, 0x0660u + static_cast<uint32>( character - '0' ) );
            else
                inoutText.push_back( character );
        }
    }

    string CultureInfo::formatInteger( int64 value ) const
    {
        utf8         arrBuffer[constant::kMaxBuffer32]{};
        const uint64 magnitude = value < 0 ? static_cast<uint64>( -( value + 1 ) ) + 1u : static_cast<uint64>( value );
        const uint32 length    = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, magnitude );
        string       text;
        if ( value < 0 )
            text.push_back( '-' );
        for ( uint32 index = 0; index < length; ++index )
        {
            const uint32 remaining = length - index;
            if ( index > 0 && remaining % 3 == 0 )
                text.append( _groupSeparator );
            appendDigits( text, string_view( arrBuffer + index, 1 ) );
        }
        return text;
    }

    uint32 CultureInfo::countVisibleFraction( float64 value, uint32 minFraction, uint32 maxFraction )
    {
        utf8         arrBuffer[constant::kMaxBuffer64]{};
        const uint32 length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer64, value < 0.0 ? -value : value, static_cast<int32>( maxFraction ) );
        string_view  text( arrBuffer, length );
        const size_t dotPos = text.find( '.' );
        if ( dotPos == string_view::npos )
            return 0;
        uint32 visible = static_cast<uint32>( text.size() - dotPos - 1 );
        while ( visible > minFraction && text[dotPos + visible] == '0' )
            --visible;
        return visible;
    }

    string CultureInfo::formatDecimal( float64 value, uint32 minFraction, uint32 maxFraction ) const
    {
        const uint32      visible   = countVisibleFraction( value, minFraction, maxFraction );
        const float64     magnitude = value < 0.0 ? -value : value;
        utf8              arrBuffer[constant::kMaxBuffer64]{};
        const uint32      length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer64, magnitude, static_cast<int32>( maxFraction ) );
        string_view       digits( arrBuffer, length );
        const size_t      dotPos      = digits.find( '.' );
        const string_view integerPart = dotPos == string_view::npos ? digits : digits.substr( 0, dotPos );

        int64 integerValue{ 0 };
        (void)StringUtil::parseInt64( integerPart, integerValue );
        const bool bNegative = value < 0.0 && ( integerValue != 0 || visible > 0 );
        string     text      = formatInteger( integerValue );
        if ( bNegative )
            text.insert( text.begin(), '-' );
        if ( visible > 0 && dotPos != string_view::npos )
        {
            text.append( _decimalSeparator );
            appendDigits( text, digits.substr( dotPos + 1, visible ) );
        }
        return text;
    }

    string CultureInfo::formatPercent( float64 ratio ) const
    {
        const string number = formatDecimal( ratio * 100.0, 0, 0 );
        string       text;
        for ( const utf8 character : _percentPattern )
        {
            if ( character == '#' )
                text.append( number );
            else
                text.push_back( character );
        }
        return text;
    }

    const string& CultureInfo::findDatePattern( string_view styleName ) const
    {
        static const string s_empty{};
        if ( styleName.empty() || styleName == "medium" )
            return _dateMedium;
        if ( styleName == "short" )
            return _dateShort;
        if ( styleName == "long" || styleName == "full" )
            return _dateLong;
        return s_empty;
    }

    const string& CultureInfo::findTimePattern( string_view styleName ) const
    {
        static const string s_empty{};
        if ( styleName.empty() || styleName == "short" )
            return _timeShort;
        if ( styleName == "medium" || styleName == "long" || styleName == "full" )
            return _timeMedium;
        return s_empty;
    }

    string CultureInfo::formatDateTime( const TextDateTime& value, string_view pattern ) const
    {
        string text;
        size_t position = 0;
        while ( position < pattern.size() )
        {
            const utf8 character = pattern[position];
            if ( character == '\'' )
            {
                // `''` 는 작은따옴표 하나, 그 밖은 다음 작은따옴표까지 그대로 쓴다.
                if ( position + 1 < pattern.size() && pattern[position + 1] == '\'' )
                {
                    text.push_back( '\'' );
                    position += 2;
                    continue;
                }
                const size_t closePos = pattern.find( '\'', position + 1 );
                const size_t endPos   = closePos == string_view::npos ? pattern.size() : closePos;
                text.append( pattern.substr( position + 1, endPos - position - 1 ) );
                position = endPos == pattern.size() ? endPos : endPos + 1;
                continue;
            }
            const bool bLetter = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' );
            if ( bLetter == false )
            {
                text.push_back( character );
                ++position;
                continue;
            }
            uint32 runLength = 0;
            while ( position + runLength < pattern.size() && pattern[position + runLength] == character )
                ++runLength;
            position += runLength;

            const uint32 monthIndex = value._month >= 1 && value._month <= 12 ? static_cast<uint32>( value._month - 1 ) : 0u;
            switch ( character )
            {
                case 'y':
                {
                    const int64 year = runLength == 2 ? value._year % 100 : value._year;
                    appendDigits( text, CultureInfoInternal::makePadded( year, runLength == 2 ? 2u : runLength ) );
                    break;
                }
                case 'M':
                {
                    if ( runLength >= 4 && _listMonthName.size() == 12 )
                        text.append( _listMonthName[monthIndex] );
                    else if ( runLength == 3 && _listMonthAbbreviation.size() == 12 )
                        text.append( _listMonthAbbreviation[monthIndex] );
                    else
                        appendDigits( text, CultureInfoInternal::makePadded( value._month, runLength >= 2 ? 2u : 1u ) );
                    break;
                }
                case 'd':
                {
                    appendDigits( text, CultureInfoInternal::makePadded( value._day, runLength >= 2 ? 2u : 1u ) );
                    break;
                }
                case 'H':
                {
                    appendDigits( text, CultureInfoInternal::makePadded( value._hour, runLength >= 2 ? 2u : 1u ) );
                    break;
                }
                case 'h':
                {
                    const uint32 hour12 = value._hour % 12 == 0 ? 12u : static_cast<uint32>( value._hour % 12 );
                    appendDigits( text, CultureInfoInternal::makePadded( hour12, runLength >= 2 ? 2u : 1u ) );
                    break;
                }
                case 'm':
                {
                    appendDigits( text, CultureInfoInternal::makePadded( value._minute, runLength >= 2 ? 2u : 1u ) );
                    break;
                }
                case 's':
                {
                    appendDigits( text, CultureInfoInternal::makePadded( value._second, runLength >= 2 ? 2u : 1u ) );
                    break;
                }
                case 'a':
                {
                    if ( _listDayPeriod.size() == 2 )
                        text.append( _listDayPeriod[value._hour < 12 ? 0 : 1] );
                    break;
                }
                default:
                {
                    text.append( runLength, character );
                    break;
                }
            }
        }
        return text;
    }

    string CultureTable::normalizeCode( string_view code )
    {
        string normalized( StringUtil::trim( code ) );
        for ( utf8& character : normalized )
        {
            if ( character == '-' )
                character = '_';
            else
                character = StringUtil::toLowerChar( character );
        }
        return normalized;
    }

    string CultureTable::makeParentCode( string_view normalizedCode )
    {
        const size_t separatorPos = normalizedCode.rfind( '_' );
        if ( separatorPos == string_view::npos || separatorPos == 0 )
            return {};
        return string( normalizedCode.substr( 0, separatorPos ) );
    }

    bool CultureTable::loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError )
    {
        string       error;
        JsonDocument document;
        if ( document.parse( FileUtil::skipUtf8Bom( jsonText ), sourceName ) == false )
        {
            if ( pOutError != nullptr )
                *pOutError = document.getLastError();
            return false;
        }
        const JsonValue root     = document.getRoot();
        const JsonValue cultures = root.isObject() ? root.get( "cultures", false ) : JsonValue{};
        if ( cultures.isObject() == false || root.getMemberNames().size() != 1 )
        {
            if ( pOutError != nullptr )
                *pOutError = string( sourceName ) + ": root must be { \"cultures\": { ... } }";
            return false;
        }

        // 부모가 먼저 풀리도록 몇 바퀴 돈다. 한 바퀴에 아무것도 못 풀면 남은 것은 없는 부모 · 순환이다.
        map<string, CultureInfo> mapResolved;
        vector<string>           listPending;
        for ( const string& rawCode : cultures.getMemberNames() )
            listPending.push_back( rawCode );

        while ( listPending.empty() == false )
        {
            vector<string> listNext;
            for ( const string& rawCode : listPending )
            {
                const JsonValue entry = cultures.get( rawCode, false );
                if ( entry.isObject() == false )
                {
                    error = "culture '" + rawCode + "' must be an object";
                    break;
                }
                const string code = normalizeCode( rawCode );
                if ( code != rawCode )
                {
                    error = "culture code '" + rawCode + "' must be written as '" + code + "'";
                    break;
                }
                const string parentCode = normalizeCode( entry.get( "parent", false ).asString() );
                CultureInfo  info;
                if ( parentCode.empty() == false )
                {
                    const auto resolvedIt = mapResolved.find( parentCode );
                    const auto existingIt = _mapCulture.find( parentCode );
                    const bool bInFile    = cultures.has( parentCode, true );
                    if ( resolvedIt != mapResolved.end() )
                        info = resolvedIt->second;
                    else if ( bInFile == false && existingIt != _mapCulture.end() )
                        info = existingIt->second;
                    else
                    {
                        listNext.push_back( rawCode );
                        continue;
                    }
                }
                else if ( entry.has( "pluralRule", false ) == false )
                {
                    error = "culture '" + rawCode + "' has no parent and no pluralRule";
                    break;
                }
                info._code       = code;
                info._parentCode = parentCode;
                info._nativeName.clear();
                if ( info._pseudoMode != PseudoLocaleMode::None && entry.has( "pseudo", false ) == false )
                    info._pseudoMode = PseudoLocaleMode::None; // 의사 방식은 물려받지 않는다 — `qps_ploc` 을 부모로 둔 실제 문화권이 의사가 되면 안 된다
                string fieldError;
                if ( CultureInfoInternal::applyFields( entry, info, fieldError ) == false )
                {
                    error = "culture '" + rawCode + "': " + fieldError;
                    break;
                }
                mapResolved[code] = std::move( info );
            }
            if ( error.empty() == false )
                break;
            if ( listNext.size() == listPending.size() )
            {
                error = "culture '" + listNext.front() + "' has an unknown parent or a parent cycle";
                break;
            }
            listPending = std::move( listNext );
        }

        if ( error.empty() == false )
        {
            if ( pOutError != nullptr )
                *pOutError = string( sourceName ) + ": " + error;
            return false;
        }
        for ( auto& [code, info] : mapResolved )
            _mapCulture[code] = std::move( info );
        return true;
    }

    bool CultureTable::loadFromResource( string_view resourcePath )
    {
        string text;
        if ( ResourceUtil::readTextResource( resourcePath, text ) == false )
        {
            SW_LOG_ERROR( "Culture table '%#' cannot be read", resourcePath );
            return false;
        }
        string error;
        if ( loadFromJsonText( text, resourcePath, &error ) == false )
        {
            SW_LOG_ERROR( "Culture table is not loaded: %#", error.c_str() );
            return false;
        }
        return true;
    }

    const CultureInfo* CultureTable::findCulture( string_view code ) const
    {
        const auto it = _mapCulture.find( normalizeCode( code ) );
        return it != _mapCulture.end() ? &it->second : nullptr;
    }

    const CultureInfo* CultureTable::resolveCulture( string_view code ) const
    {
        string candidate = normalizeCode( code );
        while ( candidate.empty() == false )
        {
            const auto it = _mapCulture.find( candidate );
            if ( it != _mapCulture.end() )
                return &it->second;
            candidate = makeParentCode( candidate );
        }
        return nullptr;
    }

    void CultureTable::collectCultureCodes( vector<string>& outListCode ) const
    {
        outListCode.clear();
        for ( const auto& [code, info] : _mapCulture )
            outListCode.push_back( code );
    }
} // namespace sw
