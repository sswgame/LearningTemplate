#include "pch.h"

#include "Engine/Localization/CultureInfo.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Localization/CurrencyUtil.h"
#include "Engine/Localization/TimeZoneUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

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

            /** @brief 히브리어입니다(CLDR). one: 정수 1 이거나 정수 0 의 소수, two: 정수 2, 나머지 other. */
            static PluralCategory selectHebrew( const PluralOperands& operands )
            {
                const bool bWhole = operands._visibleFractionCount == 0;
                if ( bWhole && operands._integerDigits == 1 )
                    return PluralCategory::One;
                if ( bWhole == false && operands._integerDigits == 0 )
                    return PluralCategory::One;
                if ( bWhole && operands._integerDigits == 2 )
                    return PluralCategory::Two;
                return PluralCategory::Other;
            }

            /** @brief 페르시아어입니다(CLDR "i = 0 or n = 1"). 정수 부분이 0 이거나 정확히 1 이면 one, 나머지 other. */
            static PluralCategory selectPersian( const PluralOperands& operands )
            {
                const bool bExactOne = operands._integerDigits == 1 && operands._fractionDigits == 0;
                const bool bOne      = operands._integerDigits == 0 || bExactOne;
                return bOne ? PluralCategory::One : PluralCategory::Other;
            }

            /** @brief 영어 서수입니다. one: 1 · 21 · 101 (11 제외), two: 2 · 22 (12 제외), few: 3 · 23 (13 제외). */
            static PluralCategory selectOrdinalEnglish( const PluralOperands& operands )
            {
                const int64 mod10  = operands._integerDigits % 10;
                const int64 mod100 = operands._integerDigits % 100;
                if ( mod10 == 1 && mod100 != 11 )
                    return PluralCategory::One;
                if ( mod10 == 2 && mod100 != 12 )
                    return PluralCategory::Two;
                if ( mod10 == 3 && mod100 != 13 )
                    return PluralCategory::Few;
                return PluralCategory::Other;
            }

            /** @brief 프랑스어 서수입니다. one: 1 (1er), 나머지 other (2e …). */
            static PluralCategory selectOrdinalFrench( const PluralOperands& operands )
            {
                return operands._integerDigits == 1 ? PluralCategory::One : PluralCategory::Other;
            }

            struct RuleRow
            {
                const utf8*    _pName;
                PluralRuleFunc _pRule;
            };

            static constexpr RuleRow kArrOrdinalRule[] = {
                {   "none",           &selectNone},
                {"english", &selectOrdinalEnglish},
                { "french",  &selectOrdinalFrench},
            };

            /** @brief 통화 패턴에서 통화 기호 자리를 가리키는 `¤`(U+00A4) 의 UTF-8 입니다. */
            static constexpr const utf8* kCurrencySign = "\xC2\xA4";

            static constexpr RuleRow kArrRule[] = {
                {   "none",    &selectNone},
                {"english", &selectEnglish},
                { "french",  &selectFrench},
                {"russian", &selectRussian},
                { "polish",  &selectPolish},
                {  "czech",   &selectCzech},
                { "arabic",  &selectArabic},
                { "hebrew",  &selectHebrew},
                {"persian", &selectPersian},
            };

            static constexpr const utf8* kArrCategoryName[] = { "zero", "one", "two", "few", "many", "other" };
            static_assert( SW_COUNT_OF( kArrCategoryName ) == static_cast<size_t>( PluralCategory::Count ), "kArrCategoryName must name every PluralCategory" );

            /** @brief 규칙 이름의 함수입니다. 모르는 이름이면 nullptr 입니다. */
            static PluralRuleFunc findRule( string_view ruleName )
            {
                for ( const RuleRow& row : kArrRule )
                {
                    if ( ruleName == row._pName )
                        return row._pRule;
                }
                return nullptr;
            }

            /** @brief 서수 규칙 이름의 함수입니다(`selectordinal`). 모르는 이름이면 nullptr 입니다. */
            static PluralRuleFunc findOrdinalRule( string_view ruleName )
            {
                for ( const RuleRow& row : kArrOrdinalRule )
                {
                    if ( ruleName == row._pName )
                        return row._pRule;
                }
                return nullptr;
            }

            /** @brief 문화권 요소의 속성 이름입니다(`code` 는 문화권 코드, 나머지는 덮어쓰는 값). */
            static constexpr string_view kArrCultureAttribute[] = { "code", "parent", "nativeName", "pluralRule", "ordinalRule", "rightToLeft", "digits", "decimalSeparator",
                                                                    "groupSeparator", "percentPattern", "currencyCode", "currencyPattern", "pseudo" };
            /** @brief 문화권 요소의 자식 요소 이름입니다(패턴 묶음 둘과 문자열 목록 넷). */
            static constexpr string_view kArrCultureChild[] = { "datePatterns", "timePatterns", "monthNames", "monthAbbreviations", "dayPeriods", "fonts" };

            template <size_t N>
            static bool isListed( const string_view ( &arrName )[N], string_view name )
            {
                for ( const string_view listed : arrName )
                {
                    if ( listed == name )
                        return true;
                }
                return false;
            }

            /** @brief 문화권 요소에서 알려지지 않은 첫 속성 · 자식 이름을 @p outName 에 적고 true 를 반환합니다. 모두 알려져 있으면 false 입니다. */
            [[nodiscard]] static bool findUnknownName( const XMLNode& entry, string& outName )
            {
                for ( XMLAttribute attribute = entry.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    if ( isListed( kArrCultureAttribute, string_view{ attribute.getName() } ) == false )
                    {
                        outName = attribute.getName();
                        return true;
                    }
                }
                for ( XMLNode child = entry.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isListed( kArrCultureChild, string_view{ child.getName() } ) == false )
                    {
                        outName = child.getName();
                        return true;
                    }
                }
                return false;
            }

            /** @brief 숫자 체계의 숫자 0 의 코드 포인트입니다. 숫자 1 ~ 9 는 그 뒤에 이어진다. */
            static uint32 getDigitZeroCodePoint( NumeralSystem numeralSystem )
            {
                switch ( numeralSystem )
                {
                    case NumeralSystem::Latin:
                        return '0';
                    case NumeralSystem::ArabicIndic:
                        return 0x0660u;
                    case NumeralSystem::ExtendedArabicIndic:
                        return 0x06F0u;
                }
                return '0';
            }

            /**
             * @brief 패턴의 작은따옴표 구간을 문자 그대로 @p outText 에 쓰고 @p inoutPosition 을 구간 뒤로 옮깁니다.
             * @details `''` 는 작은따옴표 하나, 그 밖은 다음 작은따옴표까지 그대로 쓴다. 닫는 따옴표가 없으면 끝까지 쓴다.
             */
            static void appendQuotedLiteral( string& outText, string_view pattern, size_t& inoutPosition )
            {
                if ( inoutPosition + 1 < pattern.size() && pattern[inoutPosition + 1] == '\'' )
                {
                    outText.push_back( '\'' );
                    inoutPosition += 2;
                    return;
                }
                const size_t closePos = pattern.find( '\'', inoutPosition + 1 );
                const size_t endPos   = closePos == string_view::npos ? pattern.size() : closePos;
                outText.append( pattern.substr( inoutPosition + 1, endPos - inoutPosition - 1 ) );
                inoutPosition = endPos == pattern.size() ? endPos : endPos + 1;
            }

            /** @brief 2 자리 이상이 되도록 0 을 채운 ASCII 정수입니다. */
            static string makePadded( int64 value, uint32 width )
            {
                utf8         arrBuffer[constant::kMaxBuffer32]{};
                const uint32 length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, value );
                string       text( arrBuffer, length );
                while ( text.size() < width )
                {
                    text.insert( text.begin(), '0' );
                }
                return text;
            }

            /** @brief 문자열 목록 요소(`<monthNames><item>…</item></monthNames>`)를 읽습니다. 원소가 `<item>` 이 아니면 false 입니다. */
            [[nodiscard]] static bool readStringArray( const XMLNode& listNode, vector<string>& outList )
            {
                outList.clear();
                for ( XMLNode element = listNode.findChild(); element.isValid(); element = element.findNextSibling() )
                {
                    if ( string_view{ element.getName() } != "item" )
                        return false;
                    outList.push_back( string{ element.getText() } );
                }
                return true;
            }

            /** @brief 패턴 모음(`<datePatterns short="…" medium="…" long="…" />`)을 읽습니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool readPatternGroup( const XMLNode& group, string* pShort, string* pMedium, string* pLong, string& outError )
            {
                if ( group.findChild().isValid() )
                {
                    outError = "patterns take attributes only";
                    return false;
                }
                for ( XMLAttribute attribute = group.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const string_view styleName{ attribute.getName() };
                    string*           pTarget = nullptr;
                    if ( styleName == "short" )
                        pTarget = pShort;
                    else if ( styleName == "medium" )
                        pTarget = pMedium;
                    else if ( styleName == "long" )
                        pTarget = pLong;
                    if ( pTarget == nullptr )
                    {
                        outError = "unknown pattern style '" + string( styleName ) + "'";
                        return false;
                    }
                    *pTarget = string{ attribute.getValue() };
                }
                return true;
            }

            /** @brief 문자열 필드 하나(적힌 것만)를 덮어씁니다. */
            static void applyStringField( const XMLNode& entry, const utf8* pFieldName, string& outTarget )
            {
                if ( entry.findAttribute( pFieldName ) != nullptr )
                    outTarget = string{ entry.getAttributeText( pFieldName ) };
            }

            /** @brief 복수형 규칙 이름 필드(적힌 것만)를 규칙 함수로 풀어 덮어씁니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool applyRuleField( const XMLNode& entry, const utf8* pFieldName, PluralRuleFunc ( *pFindRule )( string_view ), string& outRuleName, PluralRuleFunc& outRule,
                                                      string& outError )
            {
                if ( entry.findAttribute( pFieldName ) == nullptr )
                    return true;
                const string   ruleName{ entry.getAttributeText( pFieldName ) };
                PluralRuleFunc pRule = pFindRule( ruleName );
                if ( pRule == nullptr )
                {
                    outError = string( "unknown " ) + pFieldName + " '" + ruleName + "'";
                    return false;
                }
                outRuleName = ruleName;
                outRule     = pRule;
                return true;
            }

            /** @brief 글쓰기 방향 · 숫자 체계 · 의사 로케일을 덮어씁니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool applyScriptFields( const XMLNode& entry, CultureInfo& inoutInfo, string& outError )
            {
                std::optional<bool> bRightToLeft;
                if ( entry.readOptionalAttribute( "rightToLeft", bRightToLeft ) == false )
                {
                    outError = "rightToLeft must be true or false";
                    return false;
                }
                if ( bRightToLeft.has_value() )
                    inoutInfo._bRightToLeft = *bRightToLeft;
                if ( entry.findAttribute( "digits" ) != nullptr )
                {
                    const string digitsName{ entry.getAttributeText( "digits" ) };
                    if ( digitsName == "latn" )
                        inoutInfo._numeralSystem = NumeralSystem::Latin;
                    else if ( digitsName == "arab" )
                        inoutInfo._numeralSystem = NumeralSystem::ArabicIndic;
                    else if ( digitsName == "arabext" )
                        inoutInfo._numeralSystem = NumeralSystem::ExtendedArabicIndic;
                    else
                    {
                        outError = "unknown digits '" + digitsName + "' (latn, arab, arabext)";
                        return false;
                    }
                }
                if ( entry.findAttribute( "pseudo" ) != nullptr )
                {
                    const string pseudoName{ entry.getAttributeText( "pseudo" ) };
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
                return true;
            }

            /** @brief 문자열 목록 필드 한 칸의 규칙입니다. `_expectedCount` 가 0 이 아니면 그 개수여야 합니다. */
            struct ListFieldRow
            {
                const utf8*    _pName;
                vector<string> CultureInfo::* _pMember;
                size_t                        _expectedCount;
                const utf8*                   _pError;
            };

            static constexpr ListFieldRow kArrListField[] = {
                {        "monthNames",         &CultureInfo::_listMonthName, 12,          "monthNames must be 12 strings"},
                {"monthAbbreviations", &CultureInfo::_listMonthAbbreviation, 12,  "monthAbbreviations must be 12 strings"},
                {        "dayPeriods",         &CultureInfo::_listDayPeriod,  2,           "dayPeriods must be 2 strings"},
                {             "fonts",              &CultureInfo::_listFont,  0, "fonts must be a list of <item> strings"},
            };

            /** @brief 날짜 · 시간 패턴 모음과 문자열 목록 필드를 덮어씁니다. 모양이 틀리면 false 입니다. */
            [[nodiscard]] static bool applyPatternAndListFields( const XMLNode& entry, CultureInfo& inoutInfo, string& outError )
            {
                const XMLNode datePatterns = entry.findChild( "datePatterns" );
                if ( datePatterns.isValid() && readPatternGroup( datePatterns, &inoutInfo._dateShort, &inoutInfo._dateMedium, &inoutInfo._dateLong, outError ) == false )
                    return false;
                const XMLNode timePatterns = entry.findChild( "timePatterns" );
                if ( timePatterns.isValid() && readPatternGroup( timePatterns, &inoutInfo._timeShort, &inoutInfo._timeMedium, nullptr, outError ) == false )
                    return false;
                for ( const ListFieldRow& row : kArrListField )
                {
                    const XMLNode field = entry.findChild( row._pName );
                    if ( field.isValid() == false )
                        continue;
                    vector<string>& list      = inoutInfo.*( row._pMember );
                    const bool      bBadField = readStringArray( field, list ) == false || ( row._expectedCount != 0 && list.size() != row._expectedCount );
                    if ( bBadField )
                    {
                        outError = row._pError;
                        return false;
                    }
                }
                return true;
            }

            /** @brief 한 문화권 요소의 필드를 @p inoutInfo 에 덮어씁니다(적힌 필드만). */
            [[nodiscard]] static bool applyFields( const XMLNode& entry, CultureInfo& inoutInfo, string& outError )
            {
                string unknownName;
                if ( findUnknownName( entry, unknownName ) )
                {
                    outError = "unknown field '" + unknownName + "'";
                    return false;
                }

                applyStringField( entry, "nativeName", inoutInfo._nativeName );
                if ( applyRuleField( entry, "pluralRule", &findRule, inoutInfo._pluralRuleName, inoutInfo._pPluralRule, outError ) == false )
                    return false;
                if ( applyRuleField( entry, "ordinalRule", &findOrdinalRule, inoutInfo._ordinalRuleName, inoutInfo._pOrdinalRule, outError ) == false )
                    return false;
                if ( applyScriptFields( entry, inoutInfo, outError ) == false )
                    return false;
                applyStringField( entry, "decimalSeparator", inoutInfo._decimalSeparator );
                applyStringField( entry, "groupSeparator", inoutInfo._groupSeparator );
                applyStringField( entry, "percentPattern", inoutInfo._percentPattern );
                applyStringField( entry, "currencyCode", inoutInfo._currencyCode );
                applyStringField( entry, "currencyPattern", inoutInfo._currencyPattern );
                return applyPatternAndListFields( entry, inoutInfo, outError );
            }

            /** @brief 문화권 요소 하나를 풀어 본 결과입니다. */
            enum class ResolveResult : uint8
            {
                Resolved, ///< @p inoutMapResolved 에 더했다.
                Deferred, ///< 부모가 아직 풀리지 않았다. 다음 바퀴에서 다시 본다.
                Failed,   ///< 오류를 @p outError 에 적었다.
            };

            /** @brief `<cultures>` 의 자식을 모읍니다. 요소 이름이나 코드가 틀렸거나 코드가 겹치면 @p outError 에 적고 false 입니다. */
            [[nodiscard]] static bool collectEntries( const XMLNode& cultures, vector<XMLNode>& outListPending, vector<string>& outListFileCode, string& outError )
            {
                for ( XMLNode entry = cultures.findChild(); entry.isValid(); entry = entry.findNextSibling() )
                {
                    const string rawCode{ entry.getAttributeText( "code" ) };
                    if ( string_view{ entry.getName() } != "culture" || rawCode.empty() )
                    {
                        outError = "<cultures> holds only <culture code=\"…\"> elements";
                        return false;
                    }
                    if ( VectorUtil::contains( outListFileCode, rawCode ) )
                    {
                        outError = "culture '" + rawCode + "' is written twice";
                        return false;
                    }
                    outListFileCode.push_back( rawCode );
                    outListPending.push_back( entry );
                }
                return true;
            }

            /**
             * @brief 문화권 요소 하나를 부모의 값 위에 얹어 풀고 @p inoutMapResolved 에 더합니다.
             * @param mapExisting 이미 테이블에 있는 문화권입니다. 부모가 이 파일 안에 없을 때만 부모로 씁니다.
             */
            [[nodiscard]] static ResolveResult resolveEntry( const XMLNode& entry, const vector<string>& listFileCode, const map<string, CultureInfo>& mapExisting,
                                                             map<string, CultureInfo>& inoutMapResolved, string& outError )
            {
                const string rawCode{ entry.getAttributeText( "code" ) };
                const string code = CultureTable::normalizeCode( rawCode );
                if ( code != rawCode )
                {
                    outError = "culture code '" + rawCode + "' must be written as '" + code + "'";
                    return ResolveResult::Failed;
                }
                const string parentCode = CultureTable::normalizeCode( entry.getAttributeText( "parent" ) );
                CultureInfo  info;
                if ( parentCode.empty() == false )
                {
                    const auto resolvedIt = inoutMapResolved.find( parentCode );
                    const auto existingIt = mapExisting.find( parentCode );
                    const bool bInFile    = VectorUtil::contains( listFileCode, parentCode );
                    if ( resolvedIt != inoutMapResolved.end() )
                        info = resolvedIt->second;
                    else if ( bInFile == false && existingIt != mapExisting.end() )
                        info = existingIt->second;
                    else
                        return ResolveResult::Deferred;
                }
                else if ( entry.findAttribute( "pluralRule" ) == nullptr )
                {
                    outError = "culture '" + rawCode + "' has no parent and no pluralRule";
                    return ResolveResult::Failed;
                }
                info._code       = code;
                info._parentCode = parentCode;
                info._nativeName.clear();
                if ( info._pseudoMode != PseudoLocaleMode::None && entry.findAttribute( "pseudo" ) == nullptr )
                    info._pseudoMode = PseudoLocaleMode::None; // 의사 방식은 물려받지 않는다. `qps_ploc` 을 부모로 둔 실제 문화권이 의사 문화권이 되면 안 된다
                string fieldError;
                if ( applyFields( entry, info, fieldError ) == false )
                {
                    outError = "culture '" + rawCode + "': " + fieldError;
                    return ResolveResult::Failed;
                }
                inoutMapResolved[code] = std::move( info );
                return ResolveResult::Resolved;
            }

            /** @brief @p cultures 의 문화권을 모두 풉니다. 부모가 먼저 해석되도록 몇 바퀴 돕니다. 한 바퀴에 아무것도 해석하지 못하면 남은 것은 없는 부모이거나 순환입니다. */
            [[nodiscard]] static bool resolveAll( const XMLNode& cultures, const map<string, CultureInfo>& mapExisting, map<string, CultureInfo>& outMapResolved, string& outError )
            {
                vector<XMLNode> listPending;
                vector<string>  listFileCode; // 파일에 적힌 코드(부모가 이 파일 안에 있는지 판정한다)
                if ( collectEntries( cultures, listPending, listFileCode, outError ) == false )
                    return false;
                while ( listPending.empty() == false )
                {
                    vector<XMLNode> listNext;
                    for ( const XMLNode& entry : listPending )
                    {
                        const ResolveResult result = resolveEntry( entry, listFileCode, mapExisting, outMapResolved, outError );
                        if ( result == ResolveResult::Failed )
                            return false;
                        if ( result == ResolveResult::Deferred )
                            listNext.push_back( entry );
                    }
                    if ( listNext.size() == listPending.size() )
                    {
                        outError = "culture '" + string{ listNext.front().getAttributeText( "code" ) } + "' has an unknown parent or a parent cycle";
                        return false;
                    }
                    listPending = std::move( listNext );
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
        {
            scale *= 10.0;
        }
        const float64 fraction   = ( absoluteValue - static_cast<float64>( operands._integerDigits ) ) * scale;
        operands._fractionDigits = static_cast<int64>( fraction + 0.5 );
        return operands;
    }

    const utf8* CultureInfo::getPluralKeyword( PluralCategory category )
    {
        const size_t index = static_cast<size_t>( category );
        return index < SW_COUNT_OF( CultureInfoInternal::kArrCategoryName ) ? CultureInfoInternal::kArrCategoryName[index] : "other";
    }

    bool CultureInfo::tryParsePluralKeyword( string_view keyword, PluralCategory& outCategory )
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
        , _ordinalRuleName{ "none" }
        , _decimalSeparator{ "." }
        , _groupSeparator{ "," }
        , _percentPattern{ "#%" }
        , _currencyCode{ "USD" }
        , _currencyPattern{ "\xC2\xA4#" }
        , _dateShort{ "y-MM-dd" }
        , _dateMedium{ "y-MM-dd" }
        , _dateLong{ "y-MM-dd" }
        , _timeShort{ "HH:mm" }
        , _timeMedium{ "HH:mm:ss" }
        , _listMonthName{}
        , _listMonthAbbreviation{}
        , _listDayPeriod{}
        , _listFont{}
        , _pPluralRule{ CultureInfoInternal::findRule( "none" ) }
        , _pOrdinalRule{ CultureInfoInternal::findOrdinalRule( "none" ) }
        , _numeralSystem{ NumeralSystem::Latin }
        , _pseudoMode{ PseudoLocaleMode::None }
        , _bRightToLeft{ false }
    {
    }

    PluralCategory CultureInfo::selectPlural( const PluralOperands& operands ) const
    {
        return _pPluralRule != nullptr ? _pPluralRule( operands ) : PluralCategory::Other;
    }

    PluralCategory CultureInfo::selectOrdinal( const PluralOperands& operands ) const
    {
        return _pOrdinalRule != nullptr ? _pOrdinalRule( operands ) : PluralCategory::Other;
    }

    void CultureInfo::appendDigits( string& inoutText, string_view asciiDigits ) const
    {
        for ( const utf8 character : asciiDigits )
        {
            const bool bDigit = '0' <= character && character <= '9';
            if ( bDigit && _numeralSystem != NumeralSystem::Latin )
                StringUtil::appendUTF8( inoutText, CultureInfoInternal::getDigitZeroCodePoint( _numeralSystem ) + static_cast<uint32>( character - '0' ) );
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
        appendGroupedDigits( text, string_view( arrBuffer, length ) );
        return text;
    }

    void CultureInfo::appendGroupedDigits( string& inoutText, string_view asciiDigits ) const
    {
        const size_t length = asciiDigits.size();
        for ( size_t index = 0; index < length; ++index )
        {
            const size_t remaining = length - index;
            if ( index > 0 && remaining % 3 == 0 )
                inoutText.append( _groupSeparator );
            appendDigits( inoutText, asciiDigits.substr( index, 1 ) );
        }
    }

    uint32 CultureInfo::countVisibleFraction( float64 value, uint32 minFraction, uint32 maxFraction )
    {
        // 고정 소수 표기는 정수부를 모두 쓴다(1e308 은 309 자리). 64 바이트로는 큰 값이 잘려 빈 텍스트가 된다.
        utf8         arrBuffer[constant::kMaxBuffer512]{};
        const uint32 length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer512, value < 0.0 ? -value : value, static_cast<int32>( maxFraction ) );
        string_view  text( arrBuffer, length );
        const size_t dotPos = text.find( '.' );
        if ( dotPos == string_view::npos )
            return 0;
        uint32 visible = static_cast<uint32>( text.size() - dotPos - 1 );
        while ( visible > minFraction && text[dotPos + visible] == '0' )
        {
            --visible;
        }
        return visible;
    }

    string CultureInfo::formatDecimal( float64 value, uint32 minFraction, uint32 maxFraction ) const
    {
        const uint32      visible   = countVisibleFraction( value, minFraction, maxFraction );
        const float64     magnitude = value < 0.0 ? -value : value;
        utf8              arrBuffer[constant::kMaxBuffer512]{};
        const uint32      length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer512, magnitude, static_cast<int32>( maxFraction ) );
        string_view       digits( arrBuffer, length );
        const size_t      dotPos      = digits.find( '.' );
        const string_view integerPart = dotPos == string_view::npos ? digits : digits.substr( 0, dotPos );

        // 정수부는 정수로 되읽지 않고 글자 그대로 모은다. int64 를 넘는 값(≥ 9.2e18)도 자릿수가 그대로 남는다. inf 와 nan 은 숫자가 아니므로 그대로 쓴다.
        const bool bAllDigit = integerPart.empty() == false && integerPart.find_first_not_of( "0123456789" ) == string_view::npos;
        const bool bNonZero  = integerPart.find_first_not_of( '0' ) != string_view::npos;
        string     text;
        if ( value < 0.0 && ( bNonZero || visible > 0 ) )
            text.push_back( '-' );
        if ( bAllDigit )
            appendGroupedDigits( text, integerPart );
        else
            text.append( integerPart );
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

    string CultureInfo::formatCurrency( float64 amount, string_view currencyCode ) const
    {
        const string_view   code      = currencyCode.empty() ? string_view( _currencyCode ) : currencyCode;
        const CurrencyInfo* pCurrency = CurrencyUtil::findCurrency( code );
        const uint32        digits    = pCurrency != nullptr ? pCurrency->_minorDigitCount : 2u;
        const string        symbol    = pCurrency != nullptr ? string( pCurrency->_pSymbol ) : string( code );
        string              number    = formatDecimal( amount, digits, digits );
        const bool          bNegative = number.empty() == false && number.front() == '-';
        if ( bNegative )
            number.erase( 0, 1 ); // 부호는 기호 앞이 아니라 맨 앞에 쓴다(-$3.00, -3,00 €)
        const string_view sign = CultureInfoInternal::kCurrencySign;
        string            text;
        if ( bNegative )
            text.push_back( '-' );
        size_t position = 0;
        while ( position < _currencyPattern.size() )
        {
            if ( string_view( _currencyPattern ).substr( position, sign.size() ) == sign )
            {
                text.append( symbol );
                position += sign.size();
                continue;
            }
            if ( _currencyPattern[position] == '#' )
                text.append( number );
            else
                text.push_back( _currencyPattern[position] );
            ++position;
        }
        return text;
    }

    string_view CultureInfo::findDatePattern( string_view styleName ) const
    {
        if ( styleName.empty() || styleName == "medium" )
            return _dateMedium;
        if ( styleName == "short" )
            return _dateShort;
        if ( styleName == "long" || styleName == "full" )
            return _dateLong;
        return {};
    }

    string_view CultureInfo::findTimePattern( string_view styleName ) const
    {
        if ( styleName.empty() || styleName == "short" )
            return _timeShort;
        const bool bLongTimeStyle = styleName == "medium" || styleName == "long" || styleName == "full";
        if ( bLongTimeStyle )
            return _timeMedium;
        return {};
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
                CultureInfoInternal::appendQuotedLiteral( text, pattern, position );
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
            {
                ++runLength;
            }
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
                case 'z':
                {
                    // 시간대를 모르는 값(`_bZoned` 가 꺼진 값)에는 오프셋을 쓰지 않는다. 한 글자는 UTC+9, 넷은 UTC+09:00
                    if ( value._bZoned )
                        text.append( TimeZoneUtil::formatOffset( value._utcOffsetMinutes, runLength >= 4 ) );
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

    bool CultureTable::loadFromXMLText( string_view xmlText, string_view sourceName, string* pOutError )
    {
        string      error;
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, FileUtil::skipUTF8Bom( xmlText ), sourceName, "CultureTable", root ) != XMLRootStatus::Found )
        {
            if ( pOutError != nullptr )
            {
                *pOutError = document.getLastError();
                if ( pOutError->empty() )
                    *pOutError = string( sourceName ) + ": root must be <CultureTable>";
            }
            return false;
        }
        const XMLNode cultures           = root.findChild( "cultures" );
        const bool    bRootHasOtherParts = root.getFirstAttribute().isValid() || cultures.findNextSibling().isValid() || cultures.getFirstAttribute().isValid();
        if ( cultures.isValid() == false || bRootHasOtherParts )
        {
            if ( pOutError != nullptr )
                *pOutError = string( sourceName ) + ": root must be <CultureTable><cultures><culture code=\"…\" …/>…</cultures></CultureTable>";
            return false;
        }

        map<string, CultureInfo> mapResolved;
        if ( CultureInfoInternal::resolveAll( cultures, _mapCulture, mapResolved, error ) == false )
        {
            if ( pOutError != nullptr )
                *pOutError = string( sourceName ) + ": " + error;
            return false;
        }
        for ( auto& [code, info] : mapResolved )
        {
            _mapCulture[code] = std::move( info );
        }
        return true;
    }

    bool CultureTable::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR( "Culture table '%#' cannot be read", assetPath );
            return false;
        }
        string error;
        if ( loadFromXMLText( text, assetPath, &error ) == false )
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
        {
            outListCode.push_back( code );
        }
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( CultureTable, CultureTable::kAssetType );
} // namespace sw
