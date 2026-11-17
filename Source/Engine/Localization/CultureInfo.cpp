#include "pch.h"

#include "Engine/Localization/CultureInfo.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Localization/TimeZoneInfo.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/XML/XMLDocument.h"

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

            /** @brief 표 루트의 자식 요소 이름입니다(숫자 체계 · 통화는 없어도 된다 — 앞서 읽은 파일의 것을 쓴다). 문화권은 `CultureInfo` 원소다. */
            static constexpr string_view kArrTablePart[] = { "numberingSystems", "currencies", "CultureInfo" };

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

            /**
             * @brief 범주 키워드 → 규칙 글 맵(`_pluralRules` · `_ordinalRules`, 적힌 것만)을 컴파일해 @p outRules 를 통째로 바꿉니다. 부모의 규칙은 섞지 않는다 — 상속 규칙 표에서
             *        이 칸은 구조체 칸이 아니라 통째로 바뀐다(범주 하나만 물려받으면 규칙이 겹친다).
             * @details 키가 범주 키워드이고 값이 CLDR 규칙 글(보기 `@integer` · `@decimal` 까지, other 는 보기만)이다. 빈 맵은 "모든 수가 other" 다.
             *          모르는 범주 · 틀린 글 · 보기 없는 규칙 · other 가 빠진 묶음 · 보기와 어긋난 규칙(CLDR 과 다르게 고친 글)이면 오류입니다.
             */
            [[nodiscard]] static expected<void> compileRuleSet( const map<string, string>& mapRule, const utf8* pFieldName, PluralRuleSet& outRules )
            {
                PluralRuleSet ruleSet;
                bool          bHasOther = false;
                for ( const auto& [keyword, ruleText] : mapRule )
                {
                    if ( const expected<void> compiled = ruleSet.compileRule( keyword, ruleText ); compiled.has_value() == false )
                        return unexpected( Error{ compiled.error().kind(), compiled.error().message().data(), pFieldName }.appendDetail( ": " ).appendDetail( compiled.error().detail() ) );
                    // 규칙 글은 CLDR 글을 보기째 붙여 넣은 것이어야 한다 — 보기가 규칙과 CLDR plurals.xml 의 대조다(`PluralRuleSet::verifySamples`).
                    PluralCategory category = PluralCategory::Other;
                    const bool     bKnown   = CultureInfo::tryParsePluralKeyword( keyword, category );
                    SW_ASSERT( bKnown ); // `compileRule` 이 모르는 범주를 거절했다
                    (void)bKnown;
                    if ( ruleSet.countSamples( category ) == 0 )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a category has no @integer / @decimal samples - paste the whole CLDR rule text", pFieldName }
                                               .appendDetail( ": " )
                                               .appendDetail( keyword ) );
                    bHasOther = bHasOther || category == PluralCategory::Other;
                }
                if ( mapRule.empty() == false && bHasOther == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "other samples are missing - paste every category of the CLDR rules, other included", pFieldName } );
                const expected<void> samplesMatch = ruleSet.verifySamples();
                if ( samplesMatch.has_value() == false )
                    return unexpected( Error{ samplesMatch.error().kind(), samplesMatch.error().message().data(), pFieldName }.appendDetail( ": " ).appendDetail( samplesMatch.error().detail() ) );
                outRules = std::move( ruleSet );
                return {};
            }

            /** @brief 개수가 정해진 문자열 목록 칸 하나의 규칙입니다. 빈 목록(뿌리가 적지 않음)은 받는다 — 패턴이 그 글자를 쓰지 않는 문화권이 있다. */
            struct ListFieldRow
            {
                vector<string> CultureInfo::* _pMember;
                size_t                        _expectedCount;
                const utf8*                   _pError;
            };

            static constexpr ListFieldRow kArrListField[] = {
                {        &CultureInfo::_listMonthName, 12,         "_listMonthName must be 12 strings"},
                {&CultureInfo::_listMonthAbbreviation, 12, "_listMonthAbbreviation must be 12 strings"},
                {        &CultureInfo::_listDayPeriod,  2,          "_listDayPeriod must be 2 strings"},
            };

            /**
             * @brief 합친(부모 없는) `<CultureInfo>` 원소 하나를 리플렉션으로 읽고, 규칙 글 · 숫자 체계 · 목록 개수를 확인합니다.
             * @details 필수 칸은 읽기 전에 모은다(`collectMissingRequired`) — 표 읽기는 까닭을 오류 값으로 돌려주는 API 다.
             */
            [[nodiscard]] static expected<CultureInfo> readCulture( const XMLNode& element, const map<string, vector<string>>& mapNumberingSystem )
            {
                const TypeInfo& type = *CultureInfo::StaticType();
                const string    rawCode{ element.getAttributeText( "_code" ) };
                vector<string>  listMissing;
                XMLSerializer::collectMissingRequired( element, type, listMissing );
                if ( listMissing.empty() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a required culture field is missing - the root of the _base chain writes it", rawCode }
                                           .appendDetail( ": " )
                                           .appendDetail( listMissing.front() ) );
                CultureInfo               info;
                vector<SchemaOrphanValue> listOrphan;
                if ( XMLSerializer::deserializeElementSoft( &info, type, element, &listOrphan ) != SoftReadResult::Read || listOrphan.empty() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a culture field is unknown or unreadable", rawCode }
                                           .appendDetail( ": " )
                                           .appendDetail( listOrphan.empty() ? string_view{} : string_view{ listOrphan.front()._writtenName } ) );
                const string code = CultureTable::normalizeCode( info._code );
                if ( code != info._code )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a culture code must be written in its normalized form", info._code }.appendDetail( " -> " ).appendDetail( code ) );
                expected<void> checked = compileRuleSet( info._pluralRules, "_pluralRules", info._pluralRuleSet );
                if ( checked.has_value() )
                    checked = compileRuleSet( info._ordinalRules, "_ordinalRules", info._ordinalRuleSet );
                if ( checked.has_value() == false )
                    return unexpected( Error{ checked.error().kind(), checked.error().message().data(), code }.appendDetail( ": " ).appendDetail( checked.error().detail() ) );
                const auto systemIt = mapNumberingSystem.find( info._numberingSystem );
                if ( systemIt == mapNumberingSystem.end() )
                    return unexpected( Error{ ErrorKind::NotFound, "unknown _numberingSystem (declare it in <numberingSystems>)", code }.appendDetail( ": " ).appendDetail( info._numberingSystem ) );
                info._listDigit = systemIt->second;
                for ( const ListFieldRow& row : kArrListField )
                {
                    const vector<string>& list = info.*( row._pMember );
                    if ( list.empty() == false && list.size() != row._expectedCount )
                        return unexpected( Error{ ErrorKind::InvalidArgument, row._pError, code } );
                }
                return info;
            }

            /**
             * @brief `<numberingSystems><numberingSystem id="arab" digits="٠١٢٣٤٥٦٧٨٩"/></numberingSystems>` 를 @p inoutMap 에 얹습니다(같은 id 는 바꾼다). 요소가 없으면 그대로 true 입니다.
             * @details CLDR numberingSystems.xml 의 numeric 체계 모양이다. 글자는 꼭 10 개(0 ~ 9 순서)이고, 한 글자가 여러 바이트여도 된다(UTF-8).
             */
            [[nodiscard]] static expected<void> readNumberingSystems( const XMLNode& systems, map<string, vector<string>>& inoutMap )
            {
                if ( systems.isValid() == false )
                    return {};
                vector<string> listFileID;
                for ( XMLNode system = systems.findChild(); system.isValid(); system = system.findNextSibling() )
                {
                    const string id{ system.getAttributeText( "id" ) };
                    const string digits{ system.getAttributeText( "digits" ) };
                    if ( string_view{ system.getName() } != "numberingSystem" || id.empty() )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "<numberingSystems> holds only <numberingSystem id=\"…\" digits=\"…\"> elements", system.getName() } );
                    if ( VectorUtil::contains( listFileID, id ) )
                        return unexpected( Error{ ErrorKind::AlreadyExists, "a numbering system is written twice", id } );
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
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a numbering system must have exactly 10 digits", id } );
                    inoutMap[id] = std::move( listDigit );
                }
                return {};
            }

            /** @brief `<currencies><currency code="KRW" symbol="₩" digits="0"/></currencies>` 를 @p inoutList 에 얹습니다(같은 코드는 바꾼다). 세 속성은 모두 필수입니다. */
            [[nodiscard]] static expected<void> readCurrencies( const XMLNode& currencies, vector<CurrencyInfo>& inoutList )
            {
                if ( currencies.isValid() == false )
                    return {};
                vector<string> listFileCode;
                for ( XMLNode entry = currencies.findChild(); entry.isValid(); entry = entry.findNextSibling() )
                {
                    CurrencyInfo currency;
                    currency._code   = string{ entry.getAttributeText( "code" ) };
                    currency._symbol = string{ entry.getAttributeText( "symbol" ) };
                    optional<int32> digits;
                    const bool      bShape = string_view{ entry.getName() } == "currency" && currency._code.empty() == false && currency._symbol.empty() == false &&
                                        entry.readOptionalAttribute( "digits", digits ) && digits.has_value() && 0 <= *digits && *digits <= 4;
                    if ( bShape == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "<currencies> holds only <currency code=\"…\" symbol=\"…\" digits=\"0..4\"> elements", currency._code } );
                    if ( VectorUtil::contains( listFileCode, currency._code ) )
                        return unexpected( Error{ ErrorKind::AlreadyExists, "a currency is written twice", currency._code } );
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
                return {};
            }

            /** @brief 판 0 의 이름과 판 1 의 칸 이름 한 쌍입니다. */
            struct RenamedName
            {
                const utf8* _pOld;
                const utf8* _pNew;
            };

            /** @brief 문화권 속성입니다(`pseudo` 는 값 철자도 바뀌어 따로 본다). */
            static constexpr RenamedName kArrRenamedAttribute[] = {
                {            "code",             "_code"},
                {          "parent",             "_base"},
                {      "nativeName",       "_nativeName"},
                {          "digits",  "_numberingSystem"},
                {"decimalSeparator", "_decimalSeparator"},
                {  "groupSeparator",   "_groupSeparator"},
                {  "percentPattern",   "_percentPattern"},
                {    "currencyCode",     "_currencyCode"},
                { "currencyPattern",  "_currencyPattern"},
                {     "rightToLeft",     "_bRightToLeft"},
            };

            /** @brief 문자열 목록 원소입니다(원소 안은 그대로 `<item>` 이다). */
            static constexpr RenamedName kArrRenamedList[] = {
                {        "monthNames",         "_listMonthName"},
                {"monthAbbreviations", "_listMonthAbbreviation"},
                {        "dayPeriods",         "_listDayPeriod"},
                {             "fonts",              "_listFont"},
            };

            /** @brief 패턴 스타일 속성입니다(`short` → `_short`). */
            static constexpr RenamedName kArrRenamedStyle[] = {
                { "short",  "_short"},
                {"medium", "_medium"},
                {  "long",   "_long"},
            };

            /** @brief 판 0 의 뿌리가 적지 않으면 코드 초기값을 쓰던 글 칸입니다. 판 1 은 필수 칸이라 판 올림이 그 값을 적는다. */
            struct RootDefaultRow
            {
                const utf8* _pName;
                string CultureInfo::* _pMember;
            };

            static constexpr RootDefaultRow kArrRootDefault[] = {
                {"_decimalSeparator", &CultureInfo::_decimalSeparator},
                {  "_groupSeparator",   &CultureInfo::_groupSeparator},
                {  "_percentPattern",   &CultureInfo::_percentPattern},
                {    "_currencyCode",     &CultureInfo::_currencyCode},
                { "_currencyPattern",  &CultureInfo::_currencyPattern},
            };

            /** @brief @p name 이 표 @p arrRow 의 옛 이름이면 새 이름, 아니면 @p pName 그대로입니다(모르는 이름은 읽기가 모르는 칸으로 거절한다). */
            template <size_t N>
            static const utf8* rename( const RenamedName ( &arrRow )[N], const utf8* pName )
            {
                for ( const RenamedName& row : arrRow )
                {
                    if ( string_view{ pName } == row._pOld )
                        return row._pNew;
                }
                return pName;
            }

            /**
             * @brief 판 0 의 패턴 묶음(`<datePatterns short medium long/>`)을 판 1 칸(@p pNewName)으로 씁니다.
             *        뿌리면 빠진 스타일에 판 0 의 코드 초기값(@p listRootDefault, 스타일 차례)을 적습니다.
             */
            static void upgradePatterns( const XMLNode& entry, const utf8* pOldName, const utf8* pNewName, const vector<string>& listRootDefault, bool bRoot, const XMLNode& out )
            {
                const XMLNode old = entry.findChild( pOldName );
                if ( old.isValid() == false && bRoot == false )
                    return;
                const XMLNode patterns = out.appendChild( pNewName );
                for ( XMLAttribute attribute = old.isValid() ? old.getFirstAttribute() : XMLAttribute{}; attribute.isValid(); attribute = attribute.getNext() )
                {
                    patterns.appendAttribute( rename( kArrRenamedStyle, attribute.getName() ), attribute.getValue() );
                }
                for ( size_t index = 0; bRoot && index < listRootDefault.size(); ++index )
                {
                    if ( patterns.findAttribute( kArrRenamedStyle[index]._pNew ) == nullptr )
                        patterns.appendAttribute( kArrRenamedStyle[index]._pNew, string_view{ listRootDefault[index] } );
                }
            }

            /** @brief 판 0 의 규칙 묶음(`<pluralRules one="…"/>`)을 판 1 맵(`<_pluralRules><entry key="one">…</entry></_pluralRules>`)으로 씁니다. 자식 원소가 있으면 false 입니다. */
            [[nodiscard]] static bool upgradeRules( const XMLNode& entry, const utf8* pOldName, const utf8* pNewName, const XMLNode& out )
            {
                const XMLNode old = entry.findChild( pOldName );
                if ( old.isValid() == false )
                    return true;
                if ( old.findChild().isValid() )
                    return false; // 판 0 도 속성만 받았다
                const XMLNode rules = out.appendChild( pNewName );
                for ( XMLAttribute attribute = old.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const XMLNode rule = rules.appendChild( kXMLEntryTag );
                    rule.appendAttribute( kXMLKeyAttr, attribute.getName() );
                    rule.setValue( attribute.getValue() );
                }
                return true;
            }

            /**
             * @brief 판 0 의 `<culture>` 하나를 판 1 의 `<CultureInfo>` 로 @p out 에 씁니다. 판 0 의 읽기 규칙을 데이터로 옮긴다 —
             *        뿌리가 적지 않은 글 칸 · 패턴은 코드 초기값, 자식은 이름(`nativeName`)을 물려받지 않고, 의사 방식은 부모가 의사 문화권일 때만 끊는다.
             */
            [[nodiscard]] static bool upgradeEntry( const XMLNode& entry, const vector<string>& listPseudoCode, const XMLNode& out )
            {
                const bool bRoot = entry.findAttribute( "parent" ) == nullptr;
                for ( XMLAttribute attribute = entry.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    if ( string_view{ attribute.getName() } != "pseudo" )
                    {
                        out.appendAttribute( rename( kArrRenamedAttribute, attribute.getName() ), attribute.getValue() );
                        continue;
                    }
                    const string_view value{ attribute.getValue() };
                    if ( value != "accented" && value != "mirrored" )
                        return false;
                    out.appendAttribute( "_pseudoMode", value == "accented" ? "Accented" : "Mirrored" );
                }
                const CultureInfo defaults;
                if ( bRoot )
                {
                    for ( const RootDefaultRow& row : kArrRootDefault )
                    {
                        if ( out.findAttribute( row._pName ) == nullptr )
                            out.appendAttribute( row._pName, string_view{ defaults.*( row._pMember ) } );
                    }
                }
                else
                {
                    if ( out.findAttribute( "_nativeName" ) == nullptr )
                        out.appendAttribute( "_nativeName", "" );
                    const string parentCode = CultureTable::normalizeCode( entry.getAttributeText( "parent" ) );
                    if ( out.findAttribute( "_pseudoMode" ) == nullptr && VectorUtil::contains( listPseudoCode, parentCode ) )
                        out.appendAttribute( "_pseudoMode", "None" );
                }
                if ( upgradeRules( entry, "pluralRules", "_pluralRules", out ) == false || upgradeRules( entry, "ordinalRules", "_ordinalRules", out ) == false )
                    return false;
                upgradePatterns( entry, "datePatterns", "_datePatterns", { defaults._datePatterns._short, defaults._datePatterns._medium, defaults._datePatterns._long }, bRoot, out );
                upgradePatterns( entry, "timePatterns", "_timePatterns", { defaults._timePatterns._short, defaults._timePatterns._medium }, bRoot, out );
                for ( XMLNode child = entry.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    const string_view name{ child.getName() };
                    const bool        bConverted = name == "pluralRules" || name == "ordinalRules" || name == "datePatterns" || name == "timePatterns";
                    if ( bConverted == false )
                        out.appendClone( child ).setName( rename( kArrRenamedList, child.getName() ) );
                }
                return true;
            }

            /**
             * @brief 판 0 → 1 판 올림입니다(`SW_ASSET_FORMAT_UPGRADE`). `<cultures>` 래퍼 안의 `<culture code parent …>` 를 루트 바로 아래의
             *        `<CultureInfo _code _base …>`(리플렉션 칸 이름, 카탈로그 항목)로 옮긴다. 판 0 도 거절하던 모양(틀린 의사 방식 철자, 규칙 묶음 안의 원소)이면 false 입니다.
             */
            [[nodiscard]] static bool upgradeVersion0( FormatUpgradePayload& payload )
            {
                const XMLNode root     = payload._root;
                const XMLNode cultures = root.findChild( "cultures" );
                if ( cultures.isValid() == false )
                    return true;
                vector<string> listPseudoCode; // 판 0 은 의사 방식을 물려주지 않았다 — 그 부모를 가리키는 자식만 끊는다
                for ( XMLNode entry = cultures.findChild(); entry.isValid(); entry = entry.findNextSibling() )
                {
                    if ( entry.findAttribute( "pseudo" ) != nullptr )
                        listPseudoCode.push_back( CultureTable::normalizeCode( entry.getAttributeText( "code" ) ) );
                }
                for ( XMLNode entry = cultures.findChild(); entry.isValid(); entry = entry.findNextSibling() )
                {
                    if ( string_view{ entry.getName() } != "culture" )
                    {
                        (void)root.appendClone( entry ); // 이름 그대로 — 읽기가 모르는 원소로 거절한다
                        continue;
                    }
                    if ( upgradeEntry( entry, listPseudoCode, root.appendChild( CultureInfo::StaticType()->_name.c_str() ) ) == false )
                        return false;
                }
                return root.removeChild( cultures );
            }

            /** @brief 합친 표 루트의 `<CultureInfo>` 를 모두 읽어 @p outMapCulture 에 둡니다. 겹친 코드는 오류입니다. */
            [[nodiscard]] static expected<void> readCultures( const XMLNode& root, const map<string, vector<string>>& mapNumberingSystem, map<string, CultureInfo>& outMapCulture )
            {
                const utf8* const pElementName = CultureInfo::StaticType()->_name.c_str();
                for ( XMLNode element = root.findChild( pElementName ); element.isValid(); element = element.findNextSibling( pElementName ) )
                {
                    expected<CultureInfo> read = readCulture( element, mapNumberingSystem );
                    if ( read.has_value() == false )
                        return unexpected( read.error() );
                    if ( outMapCulture.find( read->_code ) != outMapCulture.end() )
                        return unexpected( Error{ ErrorKind::AlreadyExists, "a culture is written twice", read->_code } );
                    string code = read->_code;
                    outMapCulture.emplace( std::move( code ), std::move( *read ) );
                }
                return {};
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
        , _nativeName{}
        , _numberingSystem{ "latn" }
        , _decimalSeparator{ "." }
        , _groupSeparator{ "," }
        , _percentPattern{ "#%" }
        , _currencyCode{ "USD" }
        , _currencyPattern{ "\xC2\xA4#" }
        , _datePatterns{}
        , _timePatterns{}
        , _listMonthName{}
        , _listMonthAbbreviation{}
        , _listDayPeriod{}
        , _listFont{}
        , _pluralRules{}
        , _ordinalRules{}
        , _pseudoMode{ PseudoLocaleMode::None }
        , _bRightToLeft{ SW_FALSE }
        , _listDigit{ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" }
        , _pluralRuleSet{}
        , _ordinalRuleSet{}
        , _pCurrencyList{}
    {
        // 불변 문화권(`CultureInfo{}`)의 패턴이다. 표 읽기는 이 값으로 빈 칸을 메우지 않는다(필수 칸) — 판 0 의 판 올림만 옛 규칙대로 뿌리에 적는다.
        _datePatterns._short  = "y-MM-dd";
        _datePatterns._medium = "y-MM-dd";
        _datePatterns._long   = "y-MM-dd";
        _timePatterns._short  = "HH:mm";
        _timePatterns._medium = "HH:mm:ss";
    }

    PluralCategory CultureInfo::selectPlural( const PluralOperands& operands ) const
    {
        return _pluralRuleSet.select( operands );
    }

    PluralCategory CultureInfo::selectOrdinal( const PluralOperands& operands ) const
    {
        return _ordinalRuleSet.select( operands );
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
            return _datePatterns._medium;
        if ( styleName == "short" )
            return _datePatterns._short;
        if ( styleName == "long" || styleName == "full" )
            return _datePatterns._long;
        return {};
    }

    string_view CultureInfo::findTimePattern( string_view styleName ) const
    {
        if ( styleName.empty() || styleName == "short" )
            return _timePatterns._short;
        const bool bLongTimeStyle = styleName == "medium" || styleName == "long" || styleName == "full";
        if ( bLongTimeStyle )
            return _timePatterns._medium;
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

    expected<void> CultureTable::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        // 판을 먼저 올린다 — 판 0 의 `parent` 는 판 1 의 `_base` 가 되어야 아래 합치기가 본다.
        XMLDocument       source;
        const string_view bodyText = FileUtil::skipUTF8Bom( xmlText );
        if ( source.parse( bodyText, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the document is not well-formed XML", source.getLastError() } );
        const XMLNode sourceRoot = source.getRoot( "CultureTable" );
        if ( sourceRoot.isValid() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "root must be <CultureTable>", sourceName } );
        const bool bUpgrade = AssetXMLFormat::readXMLVersion( sourceRoot ) != kAssetType._formatVersion;
        if ( bUpgrade && AssetXMLFormat::upgradeToCurrentXMLVersion( kAssetType, sourceRoot ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the culture table format version cannot be raised to the current one", sourceName } );

        // 문화권의 부모 병합은 카탈로그 항목 상속이다(`datacataloginheritance.xml` 의 `CultureTable` 줄) — 순서 · 순환 · 없는 부모도 거기서 오류다.
        const string upgradedText = bUpgrade ? source.saveToString() : string{};
        XMLDocument  document;
        if ( document.parseData( bUpgrade ? string_view{ upgradedText } : bodyText, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the culture table could not be read or its _base chain could not be flattened", document.getLastError() } );
        const XMLNode root = document.getRoot( "CultureTable" );
        string        unknownPart;
        for ( XMLNode part = root.findChild(); part.isValid() && unknownPart.empty(); part = part.findNextSibling() )
        {
            if ( CultureInfoInternal::isListed( CultureInfoInternal::kArrTablePart, string_view{ part.getName() } ) == false )
                unknownPart = part.getName();
        }
        for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid() && unknownPart.empty(); attribute = attribute.getNext() )
        {
            if ( string_view{ attribute.getName() } != AssetXMLFormat::kXMLAttrName )
                unknownPart = attribute.getName();
        }
        if ( unknownPart.empty() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "root must be <CultureTable formatVersion>[<numberingSystems>][<currencies>]<CultureInfo _code=\"…\" …/>…</CultureTable>", sourceName }
                                   .appendDetail( ": unknown <" )
                                   .appendDetail( unknownPart )
                                   .appendDetail( ">" ) );

        // 숫자 체계 · 통화를 먼저 사본에 얹는다 — 문화권이 id 로 가리키고, 어느 하나라도 틀리면 표를 바꾸지 않는다.
        map<string, vector<string>> mapNumberingSystem = _mapNumberingSystem;
        vector<CurrencyInfo>        listCurrency       = _pCurrencyList != nullptr ? *_pCurrencyList : vector<CurrencyInfo>{};
        map<string, CultureInfo>    mapRead;
        expected<void>              read = CultureInfoInternal::readNumberingSystems( root.findChild( "numberingSystems" ), mapNumberingSystem );
        if ( read.has_value() )
            read = CultureInfoInternal::readCurrencies( root.findChild( "currencies" ), listCurrency );
        if ( read.has_value() )
            read = CultureInfoInternal::readCultures( root, mapNumberingSystem, mapRead );
        if ( read.has_value() == false )
        {
            read.error().prependDetail( ": " ).prependDetail( sourceName );
            return read;
        }
        _mapNumberingSystem = std::move( mapNumberingSystem );
        _pCurrencyList      = make_shared<const vector<CurrencyInfo>>( std::move( listCurrency ) );
        for ( auto& [code, info] : mapRead )
        {
            _mapCulture[code] = std::move( info );
        }
        for ( auto& [code, info] : _mapCulture )
        {
            info._pCurrencyList = _pCurrencyList; // 앞서 읽은 문화권도 새 통화를 본다
        }
        return {};
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
        if ( const expected<void> loaded = loadFromXMLText( text, assetPath ); loaded.has_value() == false )
        {
            SW_LOG_ERROR_ID( "CultureInfo.CultureTableNotLoaded", "Culture table is not loaded: %# (%#)", loaded.error().message(), loaded.error().detail() );
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
    SW_ASSET_FORMAT_UPGRADE( cultureTableVersion0, "CultureTable", 0, 1, &CultureInfoInternal::upgradeVersion0 );
} // namespace sw
