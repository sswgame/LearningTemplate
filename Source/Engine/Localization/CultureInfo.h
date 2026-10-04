/**
 * @file CultureInfo.h
 * @brief 문화권(culture) 데이터 — 복수형 규칙 · 숫자 · 날짜 형식 · 쓰기 방향 · 글꼴 대체 목록 — 과 그 표(`*.cultures.json`)입니다.
 * @details 숫자 기호 · 날짜 패턴 · 월 이름 · 글꼴 목록은 데이터이고, 코드는 이름으로 고르는 복수형 규칙 등록부(`PluralRuleUtil`)만 갖습니다.
 *          ICU · CLDR 를 들이지 않고 그 형식의 부분 집합을 따릅니다(패턴 기호 y · M · d · H · h · m · s · a).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief CLDR 복수형 범주입니다. 이름은 ICU MessageFormat 의 키워드(`zero` · `one` · `two` · `few` · `many` · `other`)와 같습니다. */
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
        Accented, ///< 악센트 + 길이 늘림 + 괄호 — 잘림 · 하드코딩 글자를 찾는다(`qps-ploc`)
        Mirrored  ///< 위에 더해 오른쪽→왼쪽 표시 문자로 감싼다 — RTL 배치를 본다(`qps-plocm`)
    };

    /** @brief 숫자를 어떤 숫자 글자로 쓰는지입니다(`digits`: `latn` · `arab`). */
    enum class NumeralSystem : uint8
    {
        Latin = 0,
        ArabicIndic ///< U+0660..U+0669
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PluralOperands
     * @brief CLDR 복수형 피연산자(n · i · v · f)입니다. t 는 쓰는 규칙이 없어 두지 않습니다.
     */
    struct SW_API PluralOperands
    {
        float64 _absoluteValue{ 0.0 };      ///< n — 절댓값
        int64   _integerDigits{ 0 };        ///< i — 정수 부분
        int64   _fractionDigits{ 0 };       ///< f — 보이는 소수 자리(뒤 0 포함)를 정수로
        uint32  _visibleFractionCount{ 0 }; ///< v — 보이는 소수 자리 수

        /** @brief 정수의 피연산자입니다(v = 0). */
        static PluralOperands makeInteger( int64 value );
        /** @brief 소수 @p visibleFractionCount 자리로 보일 실수의 피연산자입니다. */
        static PluralOperands makeDecimal( float64 value, uint32 visibleFractionCount );
    };
} // namespace sw

namespace sw
{
    using PluralRuleFunc = PluralCategory ( * )( const PluralOperands& operands );

    /**
     * @struct PluralRuleUtil
     * @brief 이름으로 고르는 복수형 규칙 등록부와 범주 이름입니다. 데이터(`pluralRule`)가 고르는 이름은 여기 있는 것뿐이고, 모르는 이름은 로드 오류입니다.
     * @details `none`(ko · ja · zh — other 하나) · `english`(one: i=1, v=0) · `french`(one: i=0,1) · `russian`(ru · uk) · `polish` · `czech` · `arabic`.
     */
    struct SW_API PluralRuleUtil
    {
        /** @brief 규칙 이름의 함수입니다. 모르는 이름이면 nullptr 입니다. */
        static PluralRuleFunc findRule( string_view ruleName );
        /** @brief 등록된 규칙 이름을 모읍니다. */
        static void collectRuleNames( vector<string>& outListName );
        /** @brief 범주의 키워드(`one` …)입니다. */
        static const utf8* getCategoryName( PluralCategory category );
        /** @brief 키워드를 범주로 읽습니다. 모르는 키워드면 false 입니다. */
        [[nodiscard]] static bool tryParseCategory( string_view keyword, PluralCategory& outCategory );
    };
} // namespace sw

namespace sw
{
    /** @brief 날짜 · 시각 값입니다(달력은 그레고리력, 시간대 없음 — 받은 값을 그대로 씁니다). */
    struct TextDateTime
    {
        int32 _year{ 1970 };
        uint8 _month{ 1 }; ///< 1..12
        uint8 _day{ 1 };   ///< 1..31
        uint8 _hour{ 0 };  ///< 0..23
        uint8 _minute{ 0 };
        uint8 _second{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CultureInfo
     * @brief 문화권 하나의 형식 데이터입니다. 부모(`parent`)에서 적지 않은 칸을 물려받은 뒤의 값입니다.
     */
    struct SW_API CultureInfo
    {
        string           _code;       ///< 정본 철자(`ko_kr`)
        string           _parentCode; ///< 물려받는 문화권(없으면 빈 값)
        string           _nativeName; ///< 자기 언어로 쓴 이름(언어 선택 메뉴)
        string           _pluralRuleName;
        string           _decimalSeparator;
        string           _groupSeparator;
        string           _percentPattern; ///< `#` 자리에 숫자가 들어간다(`#%` · `# %`)
        string           _dateShort;
        string           _dateMedium;
        string           _dateLong;
        string           _timeShort;
        string           _timeMedium;
        vector<string>   _listMonthName;         ///< 12 개 — 날짜 패턴의 `MMMM`
        vector<string>   _listMonthAbbreviation; ///< 12 개 — `MMM`
        vector<string>   _listDayPeriod;         ///< 2 개(오전 · 오후) — `a`
        vector<string>   _listFont;              ///< 글꼴 대체 목록(가족 이름, 앞이 우선) — UI 글꼴 렌더러가 읽는다
        PluralRuleFunc   _pPluralRule;
        NumeralSystem    _numeralSystem;
        PseudoLocaleMode _pseudoMode;
        bool             _bRightToLeft;

        CultureInfo();

        /** @brief 복수형 범주를 고릅니다. */
        PluralCategory selectPlural( const PluralOperands& operands ) const;
        /** @brief 의사 로컬라이제이션 문화권인지입니다. */
        bool isPseudo() const { return _pseudoMode != PseudoLocaleMode::None; }

        /** @brief 정수를 자리 묶음 기호와 이 문화권의 숫자 글자로 씁니다. */
        string formatInteger( int64 value ) const;
        /** @brief 실수를 소수 @p minFraction ~ @p maxFraction 자리로 씁니다(반올림, 뒤 0 은 최소 자리까지 지운다). */
        string formatDecimal( float64 value, uint32 minFraction, uint32 maxFraction ) const;
        /** @brief 비율(0.5 → 50 %)을 이 문화권의 백분율 패턴으로 씁니다. */
        string formatPercent( float64 ratio ) const;
        /** @brief 날짜 · 시각을 CLDR 패턴(`y` · `M` · `d` · `H` · `h` · `m` · `s` · `a`, 작은따옴표 글자)으로 씁니다. */
        string formatDateTime( const TextDateTime& value, string_view pattern ) const;
        /** @brief `short` · `medium` · `long` 날짜 패턴입니다. 모르는 이름이면 빈 값입니다. */
        const string& findDatePattern( string_view styleName ) const;
        /** @brief `short` · `medium` 시각 패턴입니다. 모르는 이름이면 빈 값입니다. */
        const string& findTimePattern( string_view styleName ) const;
        /** @brief 소수 실수의 보이는 소수 자리 수입니다(`formatDecimal` 과 같은 반올림). 복수형 피연산자를 만들 때 씁니다. */
        static uint32 countVisibleFraction( float64 value, uint32 minFraction, uint32 maxFraction );

    private:
        /** @brief ASCII 숫자를 이 문화권의 숫자 글자로 바꿉니다. */
        void appendDigits( string& inoutText, string_view asciiDigits ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CultureTable
     * @brief 문화권 표(`*.cultures.json`)입니다. 부모를 따라 칸을 물려받고, 지역이 붙은 코드(`ko_kr`)는 없으면 언어(`ko`)로 찾습니다.
     * @details 모르는 칸 · 모르는 복수형 규칙 · 없는 부모 · 부모 순환은 로드 오류입니다.
     */
    class SW_API CultureTable
    {
    public:
        /** @brief 문화권 표 파일의 접미사입니다. */
        static constexpr const utf8* kFileSuffix = ".cultures.json";

        /** @brief 언어 코드의 정본 철자입니다 — 소문자, `-` 는 `_`(`ko-KR` → `ko_kr`). */
        static string normalizeCode( string_view code );
        /** @brief 코드의 부모 후보(`ko_kr` → `ko`, `zh_hant_tw` → `zh_hant`)입니다. 없으면 빈 값입니다. */
        static string makeParentCode( string_view normalizedCode );

        /** @brief 표를 읽습니다(이미 있는 문화권은 바꿉니다). 실패하면 @p pOutError 에 이유를 적고 false 이며 표는 그대로입니다. */
        [[nodiscard]] bool loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError = nullptr );
        /** @brief 리소스 경로의 표를 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );

        /** @brief 정확히 그 코드의 문화권입니다(없으면 nullptr). */
        const CultureInfo* findCulture( string_view code ) const;
        /** @brief 그 코드, 없으면 지역을 뗀 부모 코드를 차례로 찾습니다(`ko_kr` → `ko`). 끝까지 없으면 nullptr 입니다. */
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
