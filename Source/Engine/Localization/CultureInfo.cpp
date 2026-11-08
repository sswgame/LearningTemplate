#include "pch.h"

#include "Engine/Localization/CultureInfo.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Localization/CurrencyInfo.h"
#include "Engine/Localization/TimeZoneInfo.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct CultureInfoInternal
        {
            /** @brief 통화 패턴에서 통화 기호 자리를 가리키는 `¤`(U+00A4) 의 UTF-8 입니다. */
            static constexpr const utf8* kCurrencySign = "\xC2\xA4";

            static constexpr const utf8* kArrCategoryName[] = { "zero", "one", "two", "few", "many", "other" };
            static_assert( SW_COUNT_OF( kArrCategoryName ) == static_cast<size_t>( PluralCategory::Count ), "kArrCategoryName must name every PluralCategory" );

            /** @brief 문화권 요소의 속성 이름입니다(`code` 는 문화권 코드, 나머지는 덮어쓰는 값). */
            static constexpr string_view kArrCultureAttribute[] = { "code", "parent", "nativeName", "rightToLeft", "digits", "decimalSeparator",
                                                                    "groupSeparator", "percentPattern", "currencyCode", "currencyPattern", "pseudo" };
            /** @brief 문화권 요소의 자식 요소 이름입니다(규칙 묶음 둘, 패턴 묶음 둘과 문자열 목록 넷). */
            static constexpr string_view kArrCultureChild[] = { "pluralRules", "ordinalRules", "datePatterns", "timePatterns", "monthNames", "monthAbbreviations", "dayPeriods", "fonts" };
            /** @brief 표 루트의 자식 요소 이름입니다(숫자 체계 · 통화는 없어도 된다 — 앞서 읽은 파일의 것을 쓴다). */
            static constexpr string_view kArrTablePart[] = { "numberingSystems", "currencies", "cultures" };

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
                utf8         arrBuffer[constant::buffer::kMax32]{};
                const uint32 length = StringUtil::formatNumber( arrBuffer, constant::buffer::kMax32, value );
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

            /**
             * @brief 규칙 묶음 요소(`<pluralRules one="…" few="…"/>`, 적힌 것만)로 @p outRules 를 통째로 바꿉니다. 부모의 규칙은 섞지 않는다 — 범주 하나만 물려받으면 규칙이 겹친다.
             * @details 속성 이름이 범주 키워드이고 값이 CLDR 규칙 글(보기 `@integer` · `@decimal` 까지, other 는 보기만)이다. 빈 요소는 "모든 수가 other" 다.
             *          모르는 범주 · 틀린 글 · 보기 없는 규칙 · other 가 빠진 묶음 · 보기와 어긋난 규칙(CLDR 과 다르게 고친 글)이면 false 입니다.
             */
            [[nodiscard]] static bool applyRuleSet( const XMLNode& entry, const utf8* pElementName, PluralRuleSet& outRules, string& outError )
            {
                const XMLNode rules = entry.findChild( pElementName );
                if ( rules.isValid() == false )
                    return true;
                if ( rules.findChild().isValid() )
                {
                    outError = string( pElementName ) + " take attributes only";
                    return false;
                }
                PluralRuleSet ruleSet;
                bool          bAnyRule  = false;
                bool          bHasOther = false;
                for ( XMLAttribute attribute = rules.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    if ( ruleSet.compileRule( attribute.getName(), attribute.getValue(), outError ) == false )
                    {
                        outError = string( pElementName ) + ": " + outError;
                        return false;
                    }
                    // 규칙 글은 CLDR 글을 보기째 붙여 넣은 것이어야 한다 — 보기가 규칙과 CLDR plurals.xml 의 대조다(`PluralRuleSet::verifySamples`).
                    PluralCategory category = PluralCategory::Other;
                    const bool     bKnown   = CultureInfo::tryParsePluralKeyword( attribute.getName(), category );
                    SW_ASSERT( bKnown ); // `compileRule` 이 모르는 범주를 거절했다
                    (void)bKnown;
                    if ( ruleSet.countSamples( category ) == 0 )
                    {
                        outError = string( pElementName ) + ": " + attribute.getName() + " has no @integer / @decimal samples - paste the whole CLDR rule text";
                        return false;
                    }
                    bAnyRule  = true;
                    bHasOther = bHasOther || category == PluralCategory::Other;
                }
                if ( bAnyRule && bHasOther == false )
                {
                    outError = string( pElementName ) + ": other samples are missing - paste every category of the CLDR rules, other included";
                    return false;
                }
                const expected<void> samplesMatch = ruleSet.verifySamples();
                if ( samplesMatch.has_value() == false )
                {
                    outError = string( pElementName ) + ": " + samplesMatch.error().message() + " (" + string( samplesMatch.error().detail() ) + ")";
                    return false;
                }
                outRules = std::move( ruleSet );
                return true;
            }

            /** @brief 글쓰기 방향 · 숫자 체계(@p mapNumberingSystem 의 id) · 의사 로케일을 덮어씁니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool applyScriptFields( const XMLNode& entry, const map<string, vector<string>>& mapNumberingSystem, CultureInfo& inoutInfo, string& outError )
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
                    const auto   systemIt = mapNumberingSystem.find( digitsName );
                    if ( systemIt == mapNumberingSystem.end() )
                    {
                        outError = "unknown digits '" + digitsName + "' (declare it in <numberingSystems>)";
                        return false;
                    }
                    inoutInfo._numberingSystem = digitsName;
                    inoutInfo._listDigit       = systemIt->second;
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
            [[nodiscard]] static bool applyFields( const XMLNode& entry, const map<string, vector<string>>& mapNumberingSystem, CultureInfo& inoutInfo, string& outError )
            {
                string unknownName;
                if ( findUnknownName( entry, unknownName ) )
                {
                    outError = "unknown field '" + unknownName + "'";
                    return false;
                }

                applyStringField( entry, "nativeName", inoutInfo._nativeName );
                if ( applyRuleSet( entry, "pluralRules", inoutInfo._pluralRules, outError ) == false )
                    return false;
                if ( applyRuleSet( entry, "ordinalRules", inoutInfo._ordinalRules, outError ) == false )
                    return false;
                if ( applyScriptFields( entry, mapNumberingSystem, inoutInfo, outError ) == false )
                    return false;
                applyStringField( entry, "decimalSeparator", inoutInfo._decimalSeparator );
                applyStringField( entry, "groupSeparator", inoutInfo._groupSeparator );
                applyStringField( entry, "percentPattern", inoutInfo._percentPattern );
                applyStringField( entry, "currencyCode", inoutInfo._currencyCode );
                applyStringField( entry, "currencyPattern", inoutInfo._currencyPattern );
                return applyPatternAndListFields( entry, inoutInfo, outError );
            }

            /**
             * @brief `<numberingSystems><numberingSystem id="arab" digits="٠١٢٣٤٥٦٧٨٩"/></numberingSystems>` 를 @p inoutMap 에 얹습니다(같은 id 는 바꾼다). 요소가 없으면 그대로 true 입니다.
             * @details CLDR numberingSystems.xml 의 numeric 체계 모양이다. 글자는 꼭 10 개(0 ~ 9 순서)이고, 한 글자가 여러 바이트여도 된다(UTF-8).
             */
            [[nodiscard]] static bool readNumberingSystems( const XMLNode& systems, map<string, vector<string>>& inoutMap, string& outError )
            {
                if ( systems.isValid() == false )
                    return true;
                vector<string> listFileID;
                for ( XMLNode system = systems.findChild(); system.isValid(); system = system.findNextSibling() )
                {
                    const string id{ system.getAttributeText( "id" ) };
                    const string digits{ system.getAttributeText( "digits" ) };
                    if ( string_view{ system.getName() } != "numberingSystem" || id.empty() )
                    {
                        outError = "<numberingSystems> holds only <numberingSystem id=\"…\" digits=\"…\"> elements";
                        return false;
                    }
                    if ( VectorUtil::contains( listFileID, id ) )
                    {
                        outError = "numbering system '" + id + "' is written twice";
                        return false;
                    }
                    listFileID.push_back( id );
                    vector<string> listDigit;
                    size_t         offset = 0;
                    while ( offset < digits.size() )
                    {
                        const size_t start = offset;
                        (void)StringUtil::decodeUTF8( digits, offset ); // 글자 하나만큼 옮긴다 — 값은 쓰지 않는다
                        listDigit.push_back( digits.substr( start, offset - start ) );
                    }
                    if ( listDigit.size() != CultureInfo::kDigitCount )
                    {
                        outError = "numbering system '" + id + "' must have exactly 10 digits";
                        return false;
                    }
                    inoutMap[id] = std::move( listDigit );
                }
                return true;
            }

            /** @brief `<currencies><currency code="KRW" symbol="₩" digits="0"/></currencies>` 를 @p inoutList 에 얹습니다(같은 코드는 바꾼다). 세 속성은 모두 필수입니다. */
            [[nodiscard]] static bool readCurrencies( const XMLNode& currencies, vector<CurrencyInfo>& inoutList, string& outError )
            {
                if ( currencies.isValid() == false )
                    return true;
                vector<string> listFileCode;
                for ( XMLNode entry = currencies.findChild(); entry.isValid(); entry = entry.findNextSibling() )
                {
                    CurrencyInfo currency;
                    currency._code   = string{ entry.getAttributeText( "code" ) };
                    currency._symbol = string{ entry.getAttributeText( "symbol" ) };
                    std::optional<int32> digits;
                    const bool           bShape = string_view{ entry.getName() } == "currency" && currency._code.empty() == false && currency._symbol.empty() == false &&
                                        entry.readOptionalAttribute( "digits", digits ) && digits.has_value() && 0 <= *digits && *digits <= 4;
                    if ( bShape == false )
                    {
                        outError = "<currencies> holds only <currency code=\"…\" symbol=\"…\" digits=\"0..4\"> elements";
                        return false;
                    }
                    if ( VectorUtil::contains( listFileCode, currency._code ) )
                    {
                        outError = "currency '" + currency._code + "' is written twice";
                        return false;
                    }
                    listFileCode.push_back( currency._code );
                    currency._minorDigitCount = static_cast<uint8>( *digits );
                    bool bReplaced            = false;
                    for ( CurrencyInfo& existing : inoutList )
                    {
                        if ( existing._code == currency._code )
                        {
                            existing  = currency;
                            bReplaced = true;
                        }
                    }
                    if ( bReplaced == false )
                        inoutList.push_back( std::move( currency ) );
                }
                return true;
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
                                                             const map<string, vector<string>>& mapNumberingSystem, map<string, CultureInfo>& inoutMapResolved, string& outError )
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
                else if ( entry.findChild( "pluralRules" ).isValid() == false || entry.findAttribute( "digits" ) == nullptr )
                {
                    outError = "culture '" + rawCode + "' has no parent, so it needs <pluralRules> (empty when every number is other) and digits";
                    return ResolveResult::Failed;
                }
                info._code       = code;
                info._parentCode = parentCode;
                info._nativeName.clear();
                if ( info._pseudoMode != PseudoLocaleMode::None && entry.findAttribute( "pseudo" ) == nullptr )
                    info._pseudoMode = PseudoLocaleMode::None; // 의사 방식은 물려받지 않는다. `qps_ploc` 을 부모로 둔 실제 문화권이 의사 문화권이 되면 안 된다
                string fieldError;
                if ( applyFields( entry, mapNumberingSystem, info, fieldError ) == false )
                {
                    outError = "culture '" + rawCode + "': " + fieldError;
                    return ResolveResult::Failed;
                }
                inoutMapResolved[code] = std::move( info );
                return ResolveResult::Resolved;
            }

            /** @brief @p cultures 의 문화권을 모두 풉니다. 부모가 먼저 해석되도록 몇 바퀴 돕니다. 한 바퀴에 아무것도 해석하지 못하면 남은 것은 없는 부모이거나 순환입니다. */
            [[nodiscard]] static bool resolveAll( const XMLNode& cultures, const map<string, CultureInfo>& mapExisting, const map<string, vector<string>>& mapNumberingSystem,
                                                  map<string, CultureInfo>& outMapResolved, string& outError )
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
                        const ResolveResult result = resolveEntry( entry, listFileCode, mapExisting, mapNumberingSystem, outMapResolved, outError );
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
        , _numberingSystem{ "latn" }
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
        , _listDigit{ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" }
        , _pluralRules{}
        , _ordinalRules{}
        , _pCurrencyList{}
        , _pseudoMode{ PseudoLocaleMode::None }
        , _bRightToLeft{ SW_FALSE }
    {
    }

    PluralCategory CultureInfo::selectPlural( const PluralOperands& operands ) const
    {
        return _pluralRules.select( operands );
    }

    PluralCategory CultureInfo::selectOrdinal( const PluralOperands& operands ) const
    {
        return _ordinalRules.select( operands );
    }

    void CultureInfo::appendDigits( string& inoutText, string_view asciiDigits ) const
    {
        SW_ASSERT( _listDigit.size() == kDigitCount && "the culture table reads exactly ten digits per numbering system" );
        for ( const utf8 character : asciiDigits )
        {
            const bool bDigit = '0' <= character && character <= '9';
            if ( bDigit )
                inoutText.append( _listDigit[static_cast<size_t>( character - '0' )] );
            else
                inoutText.push_back( character );
        }
    }

    string CultureInfo::formatInteger( int64 value ) const
    {
        utf8         arrBuffer[constant::buffer::kMax32]{};
        const uint64 magnitude = value < 0 ? static_cast<uint64>( -( value + 1 ) ) + 1u : static_cast<uint64>( value );
        const uint32 length    = StringUtil::formatNumber( arrBuffer, constant::buffer::kMax32, magnitude );
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
        utf8         arrBuffer[constant::buffer::kMax512]{};
        const uint32 length = StringUtil::formatNumber( arrBuffer, constant::buffer::kMax512, value < 0.0 ? -value : value, static_cast<int32>( maxFraction ) );
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
        utf8              arrBuffer[constant::buffer::kMax512]{};
        const uint32      length = StringUtil::formatNumber( arrBuffer, constant::buffer::kMax512, magnitude, static_cast<int32>( maxFraction ) );
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
        const CurrencyInfo* pCurrency = findCurrency( code );
        const uint32        digits    = pCurrency != nullptr ? pCurrency->_minorDigitCount : 2u;
        const string        symbol    = pCurrency != nullptr ? pCurrency->_symbol : string( code );
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

    const CurrencyInfo* CultureInfo::findCurrency( string_view code ) const
    {
        if ( _pCurrencyList == nullptr )
            return nullptr;
        for ( const CurrencyInfo& currency : *_pCurrencyList )
        {
            if ( currency._code == code )
                return &currency;
        }
        return nullptr;
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
                        text.append( TimeZoneInfo::formatOffset( value._utcOffsetMinutes, runLength >= 4 ) );
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
        const XMLNode cultures = root.findChild( "cultures" );
        string        unknownPart;
        for ( XMLNode part = root.findChild(); part.isValid() && unknownPart.empty(); part = part.findNextSibling() )
        {
            if ( CultureInfoInternal::isListed( CultureInfoInternal::kArrTablePart, string_view{ part.getName() } ) == false )
                unknownPart = part.getName();
        }
        const bool bBadRoot = cultures.isValid() == false || root.getFirstAttribute().isValid() || cultures.getFirstAttribute().isValid() || unknownPart.empty() == false;
        if ( bBadRoot )
        {
            if ( pOutError != nullptr )
                *pOutError = string( sourceName ) + ": root must be <CultureTable>[<numberingSystems>][<currencies>]<cultures><culture code=\"…\" …/>…</cultures></CultureTable>" +
                             ( unknownPart.empty() ? string{} : " (unknown <" + unknownPart + ">)" );
            return false;
        }

        // 숫자 체계 · 통화를 먼저 사본에 얹는다 — 문화권이 id 로 가리키고, 어느 하나라도 틀리면 표를 바꾸지 않는다.
        map<string, vector<string>> mapNumberingSystem = _mapNumberingSystem;
        vector<CurrencyInfo>        listCurrency       = _pCurrencyList != nullptr ? *_pCurrencyList : vector<CurrencyInfo>{};
        map<string, CultureInfo>    mapResolved;
        const bool                  bRead = CultureInfoInternal::readNumberingSystems( root.findChild( "numberingSystems" ), mapNumberingSystem, error ) &&
                           CultureInfoInternal::readCurrencies( root.findChild( "currencies" ), listCurrency, error ) &&
                           CultureInfoInternal::resolveAll( cultures, _mapCulture, mapNumberingSystem, mapResolved, error );
        if ( bRead == false )
        {
            if ( pOutError != nullptr )
                *pOutError = string( sourceName ) + ": " + error;
            return false;
        }
        _mapNumberingSystem = std::move( mapNumberingSystem );
        _pCurrencyList      = make_shared<const vector<CurrencyInfo>>( std::move( listCurrency ) );
        for ( auto& [code, info] : mapResolved )
        {
            _mapCulture[code] = std::move( info );
        }
        for ( auto& [code, info] : _mapCulture )
        {
            info._pCurrencyList = _pCurrencyList; // 앞서 읽은 문화권도 새 통화를 본다
        }
        return true;
    }

    bool CultureTable::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "CultureInfo.CultureTableCannotRead", "Culture table '%#' cannot be read", assetPath );
            return false;
        }
        string error;
        if ( loadFromXMLText( text, assetPath, &error ) == false )
        {
            SW_LOG_ERROR_ID( "CultureInfo.CultureTableNotLoaded", "Culture table is not loaded: %#", error.c_str() );
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

    const CurrencyInfo* CultureTable::findCurrency( string_view code ) const
    {
        if ( _pCurrencyList == nullptr )
            return nullptr;
        for ( const CurrencyInfo& currency : *_pCurrencyList )
        {
            if ( currency._code == code )
                return &currency;
        }
        return nullptr;
    }

    void CultureTable::clear()
    {
        _mapCulture.clear();
        _mapNumberingSystem.clear();
        _pCurrencyList.reset();
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
