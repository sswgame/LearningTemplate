/**
 * @file CultureInfo.h
 * @brief 문화권(culture) 데이터와 그 테이블(`*.cultures.json`)을 정의합니다. 복수형 규칙, 숫자와 날짜 형식, 쓰기 방향, 폰트 대체 목록을 담습니다.
 * @details 숫자 기호, 날짜 패턴, 월 이름, 폰트 목록은 데이터이고, 코드는 이름으로 고르는 복수형 규칙 표(`CultureInfo.cpp`)만 가진다.
 *          ICU 와 CLDR 를 들이지 않고 그 형식의 부분 집합을 따른다(패턴 기호는 y, M, d, H, h, m, s, a).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Common/AssetTypeRegistry.h"

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

    /** @brief 의사 로컬라이제이션 방식입니다(`*.cultures.json` 의 `pseudo`). */
    enum class PseudoLocaleMode : uint8
    {
        None = 0,
        Accented, ///< 악센트를 붙이고 길이를 늘리고 괄호로 감쌉니다. 잘림과 하드코딩 텍스트를 찾는 용도입니다(`qps-ploc`).
        Mirrored  ///< 위 처리에 더해 오른쪽에서 왼쪽 표시 문자로 감쌉니다. RTL 배치를 확인하는 용도입니다(`qps-plocm`).
    };

    /** @brief 숫자를 어떤 숫자 글자로 쓰는지 정합니다(`digits`: `latn`, `arab`, `arabext`). */
    enum class NumeralSystem : uint8
    {
        Latin = 0,
        ArabicIndic,        ///< U+0660..U+0669(아랍어)
        ExtendedArabicIndic ///< U+06F0..U+06F9(페르시아어 · 우르두어)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PluralOperands
     * @brief CLDR 복수형 피연산자(n, i, v, f)입니다. t 는 쓰는 규칙이 없어 두지 않습니다.
     */
    struct SW_API PluralOperands
    {
        float64 _absoluteValue{ 0.0 };      ///< n 이며 절댓값입니다.
        int64   _integerDigits{ 0 };        ///< i 이며 정수 부분입니다.
        int64   _fractionDigits{ 0 };       ///< f 이며 보이는 소수 자리(뒤의 0 포함)를 정수로 만든 값입니다.
        uint32  _visibleFractionCount{ 0 }; ///< v 이며 보이는 소수 자리 수입니다.

        /** @brief 정수의 피연산자입니다(v = 0). */
        static PluralOperands makeInteger( int64 value );
        /** @brief 소수 @p visibleFractionCount 자리로 보일 실수의 피연산자를 만듭니다. */
        static PluralOperands makeDecimal( float64 value, uint32 visibleFractionCount );
    };
} // namespace sw

namespace sw
{
    /** @brief 복수형 규칙 하나입니다. 데이터(`pluralRule` · `ordinalRule`)가 이름으로 고르며, 고를 수 있는 이름은 `CultureInfo.cpp` 의 규칙 표에 있는 것뿐입니다. */
    using PluralRuleFunc = PluralCategory ( * )( const PluralOperands& operands );
} // namespace sw

namespace sw
{
    /**
     * @brief 날짜와 시각 값입니다(달력은 그레고리력입니다).
     * @details 값은 받은 그대로 쓰고 시간대 변환은 하지 않습니다. 시간대가 필요하면 `TimeZoneUtil::convertUnixMilliseconds` 가 UTC 시각을 그 시간대의 값으로 바꾸고
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
    /**
     * @struct CultureInfo
     * @brief 문화권 하나의 형식 데이터입니다. 부모(`parent`)에서 적지 않은 필드를 물려받은 뒤의 값입니다.
     */
    struct SW_API CultureInfo
    {
        string           _code;       ///< 원본 철자(`ko_kr`)
        string           _parentCode; ///< 물려받는 문화권(없으면 빈 값)
        string           _nativeName; ///< 자기 언어로 쓴 이름(언어 선택 메뉴)
        string           _pluralRuleName;
        string           _ordinalRuleName;
        string           _decimalSeparator;
        string           _groupSeparator;
        string           _percentPattern;  ///< `#` 위치에 숫자가 들어갑니다(`#%`, `# %`).
        string           _currencyCode;    ///< 이 문화권의 기본 통화(ISO 4217, `KRW`)입니다.
        string           _currencyPattern; ///< `#` 위치에 숫자가, `¤` 위치에 통화 기호가 들어갑니다(`¤#`, `# ¤`).
        string           _dateShort;
        string           _dateMedium;
        string           _dateLong;
        string           _timeShort;
        string           _timeMedium;
        vector<string>   _listMonthName;         ///< 12 개이며 날짜 패턴의 `MMMM` 에 씁니다.
        vector<string>   _listMonthAbbreviation; ///< 12 개이며 `MMM` 에 씁니다.
        vector<string>   _listDayPeriod;         ///< 2 개(오전, 오후)이며 `a` 에 씁니다.
        vector<string>   _listFont;              ///< 폰트 대체 목록입니다(패밀리 이름이며 앞이 우선). UI 폰트 렌더러가 읽습니다.
        PluralRuleFunc   _pPluralRule;
        PluralRuleFunc   _pOrdinalRule;
        NumeralSystem    _numeralSystem;
        PseudoLocaleMode _pseudoMode;
        bool             _bRightToLeft;

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
         * @param currencyCode ISO 4217 코드이며 비우면 이 문화권의 기본 통화입니다. 모르는 코드는 기호 자리에 코드를 쓰고 소수 2 자리입니다.
         */
        string formatCurrency( float64 amount, string_view currencyCode ) const;
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
     * @brief 문화권 테이블(`*.cultures.json`)입니다. 부모를 따라 필드를 물려받고, 지역이 붙은 코드(`ko_kr`)는 없으면 언어(`ko`)로 찾습니다.
     * @details 모르는 필드, 모르는 복수형 규칙, 없는 부모, 부모 순환은 로드 오류다.
     */
    class SW_API CultureTable
    {
    public:
        /** @brief 문화권 테이블 파일의 접미사입니다. */
        static constexpr const utf8* kExtension = ".cultures.json";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `CultureInfo.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "CultureTable", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 언어 코드의 정규 표기입니다. 소문자로 쓰고 `-` 는 `_` 로 바꿉니다(`ko-KR` → `ko_kr`). */
        static string normalizeCode( string_view code );
        /** @brief 코드의 부모 후보(`ko_kr` → `ko`, `zh_hant_tw` → `zh_hant`)입니다. 없으면 빈 값입니다. */
        static string makeParentCode( string_view normalizedCode );

        /** @brief 테이블을 읽습니다(이미 있는 문화권은 바꿉니다). 실패하면 @p pOutError 에 이유를 적고 false 를 반환하며, 테이블은 그대로 둡니다. */
        [[nodiscard]] bool loadFromJSONText( string_view jsonText, string_view sourceName, string* pOutError = nullptr );
        /** @brief 리소스 경로의 테이블을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );

        /** @brief 정확히 그 코드의 문화권입니다(없으면 nullptr). */
        const CultureInfo* findCulture( string_view code ) const;
        /** @brief 그 코드를 찾고, 없으면 지역을 뗀 부모 코드를 차례로 찾습니다(`ko_kr` → `ko`). 끝까지 없으면 nullptr 입니다. */
        const CultureInfo* resolveCulture( string_view code ) const;
        /** @brief 표의 모든 문화권 코드입니다(사전 순). */
        void collectCultureCodes( vector<string>& outListCode ) const;
        /** @brief 문화권 수입니다. */
        size_t getCultureCount() const { return _mapCulture.size(); }
        /** @brief 비웁니다. */
        void clear() { _mapCulture.clear(); }

    private:
        map<string, CultureInfo> _mapCulture;
    };
} // namespace sw
