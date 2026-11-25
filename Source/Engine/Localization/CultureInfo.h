/**
 * @file CultureInfo.h
 * @brief 문화권(culture) 데이터와 그 테이블(`*.cultures.xml`)을 정의합니다. 복수형 규칙, 숫자 체계, 통화, 숫자와 날짜 형식, 쓰기 방향, 폰트 대체 목록을 담습니다.
 * @details 문화권의 모든 내용(복수형 · 서수 규칙 글, 숫자 글자, 통화 기호와 소수 자리, 숫자 기호, 날짜 패턴, 월 이름, 폰트 목록)은 데이터다 — 언어 하나를 더하는 일은 표에 `<CultureInfo>` 한 장이다.
 *          코드는 CLDR 형식의 부분 집합을 읽고 쓸 뿐이다(복수형 규칙 문법 `PluralRules.h`, 날짜 패턴 기호 y, M, d, H, h, m, s, a, z). ICU 는 들이지 않는다.
 */
#pragma once
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Localization/PluralRules.h"
#include "Engine/Reflection/ReflectionMacros.h"

/*
 * 통화(ISO 4217) 한 줄 — 코드 · 기호 · 소수 자리입니다. 줄은 문화권 표(`*.cultures.xml` 의 `<currencies>`)의 데이터이고, `CultureInfo::formatCurrency` 와 `{n, number, currency}` 가 읽습니다.
 * 표에 없는 통화는 찾기가 nullptr 이고, 호출자가 코드를 기호 자리에 쓰고 소수 2 자리로 다룹니다.
 *          기호는 문화권마다 달리 쓰이지만(미국 밖의 `US$`) 이 표는 한 가지 철자만 가집니다 — 문화권별 기호가 필요해지면 문화권 데이터로 덮는 칸을 더합니다(CLDR 의 문화권별 currencies).
 */
namespace sw
{
    /** @brief 통화 한 줄입니다. */
    struct CurrencyInfo
    {
        string _code{};               ///< ISO 4217 코드(`KRW`)
        string _symbol{};             ///< 화면에 쓰는 기호(`₩`)
        uint8  _minorDigitCount{ 2 }; ///< 소수 자리 수(KRW · JPY 0, USD · EUR 2). 표의 `digits` 는 필수다
    };
} // namespace sw

namespace sw
{
    /** @brief CLDR 복수형 범주입니다. 이름은 ICU MessageFormat 의 키워드(`zero`, `one`, `two`, `few`, `many`, `other`)와 같습니다. */
    enum class PluralCategory : uint8
    {
        Zero = 0,
        One,
        Two,
        Few,
        Many,
        Other,
        Count
    };

    /** @brief 의사 로컬라이제이션 방식입니다(`*.cultures.xml` 의 `_pseudoMode`). */
    ENUM()
    enum class PseudoLocaleMode : uint8
    {
        None = 0,
        Accented, ///< 악센트를 붙이고 길이를 늘리고 괄호로 감쌉니다. 잘림과 하드코딩 텍스트를 찾는 용도입니다(`qps-ploc`).
        Mirrored  ///< 위 처리에 더해 오른쪽에서 왼쪽 표시 문자로 감쌉니다. RTL 배치를 확인하는 용도입니다(`qps-plocm`).
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 날짜와 시각 값입니다(달력은 그레고리력입니다).
     * @details 값은 받은 그대로 쓰고 시간대 변환은 하지 않습니다. 시간대가 필요하면 `TimeZoneInfo::convertUnixMilliseconds` 가 UTC 시각을 그 시간대의 값으로 바꾸고
     *          `_bZoned` 와 `_utcOffsetMinutes` 를 채웁니다(패턴의 `z` 가 그 오프셋을 씁니다).
     */
    struct TextDateTime
    {
        int32 _year{ 1970 };
        int16 _utcOffsetMinutes{ 0 }; ///< UTC 와의 차이이며 `_bZoned` 일 때만 뜻이 있습니다.
        uint8 _month{ 1 };            ///< 1..12
        uint8 _day{ 1 };              ///< 1..31
        uint8 _hour{ 0 };             ///< 0..23
        uint8 _minute{ 0 };
        uint8 _second{ 0 };
        bool  _bZoned{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 날짜 패턴 묶음(CLDR dateFormats 의 short · medium · long)입니다. 물려받을 때 적은 스타일만 덮는다(상속 규칙 표의 구조체 칸). */
    REFLECT()
    struct SW_API CultureDatePatternSet
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _short;
        PROPERTY( Required )
        string _medium;
        PROPERTY( Required )
        string _long;
    };

    /** @brief 시각 패턴 묶음(CLDR timeFormats 의 short · medium)입니다. */
    REFLECT()
    struct SW_API CultureTimePatternSet
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _short;
        PROPERTY( Required )
        string _medium;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CultureInfo
     * @brief 문화권 하나의 형식 데이터입니다. 표의 `<CultureInfo>` 한 장을 부모(`_base`)와 합친 뒤 리플렉션 XML 직렬화로 읽은 값입니다.
     * @details 이 구조체의 `PROPERTY` 가 곧 스키마다 — 칸 목록을 파서가 따로 알지 않는다. 부모 병합은 카탈로그 항목 상속(`DataCatalogInheritance`,
     *          규칙 줄은 `datacataloginheritance.xml` 의 `CultureTable`)이 읽기 전에 끝내므로, 읽는 쪽에는 부모가 없다. 그래서 `Required` 가
     *          "사슬의 뿌리가 꼭 적는 칸" 이다(자식은 뿌리에서 물려받아 늘 채워진다). 생성자의 초기값은 표 밖에서 만드는 `CultureInfo{}`(불변 문화권)의 값이고,
     *          표 읽기는 그 값으로 빈 칸을 메우지 않는다(필수 칸이 빠지면 오류).
     *          복수형 규칙은 범주 키워드 → CLDR 규칙 글의 맵으로 읽고 `PluralRuleSet` 으로 컴파일한다(`_pluralRuleSet`).
     */
    REFLECT()
    struct SW_API CultureInfo
    {
        REFLECT_BODY();

        /** @brief 숫자 글자 수입니다(0 ~ 9). */
        static constexpr uint32 kDigitCount = 10;

        PROPERTY( Required, Tooltip = "Culture code in its normalized form (ko_kr) - the catalog key that _base points at" )
        string _code;
        PROPERTY( Tooltip = "Name in its own language (language menu)" )
        string _nativeName;
        PROPERTY( Required, Tooltip = "Numbering system id declared in <numberingSystems>" )
        string _numberingSystem; ///< 숫자 체계 id(`latn` · `arab` — 표의 `<numberingSystems>`)
        PROPERTY( Required )
        string _decimalSeparator;
        PROPERTY( Required )
        string _groupSeparator;
        PROPERTY( Required )
        string _percentPattern; ///< `#` 위치에 숫자가 들어갑니다(`#%`, `# %`).
        PROPERTY( Required )
        string _currencyCode; ///< 이 문화권의 기본 통화(ISO 4217, `KRW`)입니다.
        PROPERTY( Required )
        string _currencyPattern; ///< `#` 위치에 숫자가, `¤` 위치에 통화 기호가 들어갑니다(`¤#`, `# ¤`).
        PROPERTY( Required )
        CultureDatePatternSet _datePatterns;
        PROPERTY( Required )
        CultureTimePatternSet _timePatterns;
        PROPERTY()
        vector<string> _listMonthName; ///< 12 개이며 날짜 패턴의 `MMMM` 에 씁니다.
        PROPERTY()
        vector<string> _listMonthAbbreviation; ///< 12 개이며 `MMM` 에 씁니다.
        PROPERTY()
        vector<string> _listDayPeriod; ///< 2 개(오전, 오후)이며 `a` 에 씁니다.
        PROPERTY()
        vector<string> _listFont; ///< 폰트 대체 목록입니다(패밀리 이름이며 앞이 우선). UI 폰트 렌더러가 읽습니다.
        PROPERTY( Required, Tooltip = "Plural category keyword -> CLDR rule text with its samples (empty when every number is other)" )
        map<string, string> _pluralRules;
        PROPERTY( Tooltip = "Ordinal category keyword -> CLDR rule text with its samples" )
        map<string, string> _ordinalRules;
        PROPERTY()
        PseudoLocaleMode _pseudoMode;
        PROPERTY()
        uint8 _bRightToLeft : 1;

        vector<string>                         _listDigit;      ///< 숫자 체계의 글자 0 ~ 9(10 개, UTF-8 한 글자씩) — 표가 `_numberingSystem` 으로 걸어 준다
        PluralRuleSet                          _pluralRuleSet;  ///< `_pluralRules` 를 컴파일한 것(`plural`)
        PluralRuleSet                          _ordinalRuleSet; ///< `_ordinalRules` 를 컴파일한 것(`selectordinal`)
        shared_ptr<const vector<CurrencyInfo>> _pCurrencyList;  ///< 표의 통화 목록(`<currencies>`). 표가 문화권을 읽을 때 걸어 준다

        CultureInfo();

        /** @brief 복수형 범주를 고릅니다. */
        PluralCategory selectPlural( const PluralOperands& operands ) const;
        /** @brief 서수(1st · 2nd) 범주를 고릅니다. */
        PluralCategory selectOrdinal( const PluralOperands& operands ) const;
        /** @brief 복수형 범주의 키워드(`one` …, ICU MessageFormat)입니다. */
        static const utf8* getPluralKeyword( PluralCategory category );
        /** @brief 키워드를 범주로 읽습니다. 모르는 키워드면 false 입니다. */
        [[nodiscard]] static bool tryParsePluralKeyword( string_view keyword, PluralCategory& outCategory );
        /** @brief 의사 로컬라이제이션 문화권인지입니다. */
        bool isPseudo() const { return _pseudoMode != PseudoLocaleMode::None; }

        /** @brief 정수를 자릿수 구분 기호와 이 문화권의 숫자 글자로 씁니다. */
        string formatInteger( int64 value ) const;
        /** @brief 실수를 소수 @p minFraction ~ @p maxFraction 자리로 씁니다(반올림하고, 뒤의 0 은 최소 자리까지 지웁니다). */
        string formatDecimal( float64 value, uint32 minFraction, uint32 maxFraction ) const;
        /** @brief 비율(0.5 → 50 %)을 이 문화권의 백분율 패턴으로 씁니다. */
        string formatPercent( float64 ratio ) const;
        /**
         * @brief 금액을 통화 패턴으로 씁니다. 소수 자리는 통화가 정합니다(KRW · JPY 0, USD · EUR 2).
         * @param currencyCode ISO 4217 코드이며 비우면 이 문화권의 기본 통화입니다. 표에 없는 코드는 기호 자리에 코드를 쓰고 소수 2 자리입니다(ICU 가 모르는 통화를 쓰는 방식).
         */
        string formatCurrency( float64 amount, string_view currencyCode ) const;
        /** @brief 표의 통화입니다(대소문자 구분). 없으면 nullptr 입니다. */
        const CurrencyInfo* findCurrency( string_view code ) const;
        /** @brief 날짜와 시각을 CLDR 패턴(`y`, `M`, `d`, `H`, `h`, `m`, `s`, `a`, 작은따옴표 텍스트)으로 씁니다. */
        string formatDateTime( const TextDateTime& value, string_view pattern ) const;
        /** @brief `short`, `medium`, `long` 날짜 패턴입니다. 모르는 이름이면 빈 값입니다. */
        string_view findDatePattern( string_view styleName ) const;
        /** @brief `short`, `medium` 시각 패턴입니다. 모르는 이름이면 빈 값입니다. */
        string_view findTimePattern( string_view styleName ) const;
        /** @brief 소수 실수의 보이는 소수 자리 수입니다(`formatDecimal` 과 같은 반올림). 복수형 피연산자를 만들 때 씁니다. */
        static uint32 countVisibleFraction( float64 value, uint32 minFraction, uint32 maxFraction );

    private:
        /** @brief ASCII 숫자를 이 문화권의 숫자 글자로 바꿉니다. */
        void appendDigits( string& inoutText, string_view asciiDigits ) const;
        /** @brief ASCII 숫자열을 세 자리마다 이 문화권의 구분 기호로 나누고, 이 문화권의 숫자 글자로 붙입니다. */
        void appendGroupedDigits( string& inoutText, string_view asciiDigits ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CultureTable
     * @brief 문화권 테이블(`*.cultures.xml`)입니다. 문화권은 카탈로그 항목이라 `_base` 로 부모를 가리키고 적은 칸만 덮으며, 지역이 붙은 코드(`ko_kr`)는 없으면 언어(`ko`)로 찾습니다.
     * @details 형식(CLDR 의 numberingSystems · currencyData · plurals 를 한 파일에 모은 모양 — 문화권 원소는 `CultureInfo` 의 리플렉션 XML 이다):
     * @code
     *     <CultureTable formatVersion="0">
     *         <numberingSystems><numberingSystem id="latn" digits="0123456789"/><numberingSystem id="arab" digits="٠١٢٣٤٥٦٧٨٩"/></numberingSystems>
     *         <currencies><currency code="KRW" symbol="₩" digits="0"/></currencies>
     *         <CultureInfo _code="ru" _nativeName="Русский" _numberingSystem="latn" …>
     *             <_pluralRules><entry key="one">v = 0 and i % 10 = 1 and i % 100 != 11 @integer …</entry>…</_pluralRules>
     *             <_datePatterns _short="…" _medium="…" _long="…"/>
     *             <_listMonthName><item>…</item></_listMonthName>
     *         </CultureInfo>
     *         <CultureInfo _code="ru_ua" _base="ru" _nativeName="…"/>
     *     </CultureTable>
     * @endcode
     *          부모 병합(순서 · 순환 · 없는 부모)은 `DataCatalogInheritance` 가 하고, 필수 칸(`PROPERTY( Required )` — 뿌리가 적는다)은 리플렉션 스키마가 본다.
     *          모르는 칸 · 범주 · 숫자 체계, 틀린 규칙 글, 글자가 10 개가 아닌 숫자 체계, 겹친 id · 코드, 없는 부모, 부모 순환은 로드 오류이고 표는 그대로 남는다.
     *          숫자 체계와 통화는 앞서 읽은 파일의 것도 쓰고, 같은 id 는 바꾼다.
     */
    class SW_API CultureTable
    {
    public:
        /** @brief 문화권 테이블 파일의 접미사입니다. */
        static constexpr const utf8* kExtension = ".cultures.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `CultureInfo.cpp` 의 `SW_ASSET_TYPE`). 판 1 은 문화권을 리플렉션 원소 · `_base` 상속으로 적는다. */
        static constexpr AssetTypeDef kAssetType{ "CultureTable", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 언어 코드의 정규 표기입니다. 소문자로 쓰고 `-` 는 `_` 로 바꿉니다(`ko-KR` → `ko_kr`). */
        static string normalizeCode( string_view code );
        /** @brief 코드의 부모 후보(`ko_kr` → `ko`, `zh_hant_tw` → `zh_hant`)입니다. 없으면 빈 값입니다. */
        static string makeParentCode( string_view normalizedCode );

        /** @brief 테이블을 읽습니다(이미 있는 문화권은 바꿉니다). 실패하면 그 까닭(대상 앞에 원본 이름)이고, 테이블은 그대로 둡니다. */
        [[nodiscard]] expected<void> loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 경로의 테이블을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );

        /** @brief 정확히 그 코드의 문화권입니다(없으면 nullptr). */
        const CultureInfo* findCulture( string_view code ) const;
        /** @brief 그 코드를 찾고, 없으면 지역을 뗀 부모 코드를 차례로 찾습니다(`ko_kr` → `ko`). 끝까지 없으면 nullptr 입니다. */
        const CultureInfo* resolveCulture( string_view code ) const;
        /** @brief 표의 모든 문화권 코드입니다(사전 순). */
        void collectCultureCodes( vector<string>& outListCode ) const;
        /** @brief 문화권 수입니다. */
        size_t getCultureCount() const { return _mapCulture.size(); }
        /** @brief 표의 통화입니다(대소문자 구분). 없으면 nullptr 입니다. */
        const CurrencyInfo* findCurrency( string_view code ) const;
        /** @brief 비웁니다. */
        void clear();

    private:
        map<string, CultureInfo>               _mapCulture;
        map<string, vector<string>>            _mapNumberingSystem; ///< 숫자 체계 id → 글자 0 ~ 9
        shared_ptr<const vector<CurrencyInfo>> _pCurrencyList;      ///< 문화권이 나눠 든다(읽을 때마다 새 목록으로 바꿔 건다)
    };
} // namespace sw
