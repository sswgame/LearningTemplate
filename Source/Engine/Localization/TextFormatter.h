/**
 * @file TextFormatter.h
 * @brief ICU MessageFormat 의 부분 집합을 정의합니다. 이름 인자, 복수형(plural), 서수(selectordinal), 성별 등 고르기(select), 숫자, 화폐, 날짜, 시각을 지원합니다.
 * @details 문법은 ICU 와 같다(번역가 도구, Crowdin, Phrase 가 그대로 읽는다).
 *          - `{name}` 이름 인자. 숫자 인자는 문화권 숫자 형식, 날짜 인자는 `medium` 날짜로 쓴다.
 *          - `{n, number}`, `{n, number, integer}`, `{n, number, percent}`, `{n, number, currency}`(문화권의 기본 통화), `{n, number, ::currency/USD}`(통화 지정)
 *          - `{d, date, short|medium|long}`, `{d, time, short|medium|long}`. 시간대를 아는 값(`addUnixTime`)은 time 의 `long` 이 `UTC+9` 같은 오프셋을 붙인다.
 *          - `{n, plural, offset:1 =0 {없음} one {# 개} other {# 개}}`. `#` 은 (값 - offset) 을 문화권 숫자로 쓰며, `=N` 이 범주보다 앞선다.
 *          - `{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}`. 서수 범주는 문화권의 `ordinalRule` 이 고른다(offset 은 없다).
 *          - `{g, select, female {그녀} male {그} other {그들}}`
 *          - 작은따옴표: `''` 는 `'` 하나, `'{'` 처럼 `{` `}` (복수형 안에서는 `#`) 앞의 따옴표는 다음 따옴표까지 글자 그대로(ICU DOUBLE_OPTIONAL).
 *          `spellout` 은 없다. plural · selectordinal · select 에는 `other` 가 반드시 있어야 한다(ICU 와 같다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Localization/CultureInfo.h"

namespace sw
{
    /** @brief 인자 값의 종류입니다. */
    enum class TextArgumentKind : uint8
    {
        Text = 0,
        Integer,
        Decimal,
        DateTime
    };
} // namespace sw

namespace sw
{
    /** @brief 이름 붙은 인자 하나입니다. */
    struct TextArgument
    {
        string           _name;
        string           _text;
        TextDateTime     _dateTime{};
        float64          _decimal{ 0.0 };
        int64            _integer{ 0 };
        TextArgumentKind _kind{ TextArgumentKind::Text };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TextArgumentList
     * @brief 포맷 인자 목록입니다. `TextArgumentList().addText( "name", "Bob" ).addInteger( "count", 3 )` 처럼 잇습니다.
     * @details 같은 이름을 다시 넣으면 앞의 값을 바꿉니다.
     */
    class SW_API TextArgumentList
    {
    public:
        TextArgumentList& addText( string_view name, string_view value );
        TextArgumentList& addInteger( string_view name, int64 value );
        TextArgumentList& addDecimal( string_view name, float64 value );
        TextArgumentList& addDateTime( string_view name, const TextDateTime& value );
        /**
         * @brief UTC 유닉스 시각(밀리초)을 @p zoneName 시간대의 날짜 · 시각 인자로 넣습니다(`WallClock::nowUnixMilliseconds()` 를 그대로 받는다).
         * @details 표에 없는 시간대 이름(`TimeZoneUtil::findZone`)은 UTC 로 씁니다.
         */
        TextArgumentList& addUnixTime( string_view name, int64 unixMilliseconds, string_view zoneName );

        /** @brief 그 이름의 인자입니다(없으면 nullptr). */
        const TextArgument* findArgument( string_view name ) const;
        /** @brief 인자 수입니다. */
        size_t getCount() const { return _listArgument.size(); }

    private:
        TextArgument& getOrAddArgument( string_view name );

    private:
        vector<TextArgument> _listArgument;
    };
} // namespace sw

namespace sw
{
    /** @brief 패턴의 텍스트 조각(구문이 아닌 부분)을 바꾸는 함수입니다. 조각은 따옴표가 포함된 원문 그대로이고, 반환값이 그 위치에 들어갑니다. */
    using TextLiteralMapFunc = string ( * )( string_view literalText );

    /**
     * @struct TextFormatter
     * @brief 메시지 패턴을 문화권에 맞춰 해석합니다. 상태가 없습니다.
     */
    struct SW_API TextFormatter
    {
        /**
         * @brief @p pattern 을 @p arguments 로 해석해 @p outText 에 씁니다.
         * @return 구문 오류, 없는 인자, 맞지 않는 인자 종류가 있으면 false 이고 이유를 @p pOutError 에 적습니다.
         *         그래도 @p outText 는 채웁니다(없는 인자는 `{name}` 그대로). 화면이 비지 않게 하기 위해서입니다.
         */
        static bool format( string_view pattern, const TextArgumentList& arguments, const CultureInfo& culture, string& outText, string* pOutError = nullptr );
        /** @brief 구문만 검사합니다(인자 없이). */
        [[nodiscard]] static bool validatePattern( string_view pattern, string* pOutError = nullptr );
        /** @brief 패턴이 쓰는 인자 이름입니다(사전 순이며 중복 없음). 구문 오류면 읽은 데까지입니다. 원문과 번역의 플레이스홀더가 같은지 확인할 때 씁니다. */
        static void collectArgumentNames( string_view pattern, vector<string>& outListName );
        /** @brief 구문은 그대로 두고 텍스트 조각만 @p pfnMap 으로 바꾼 패턴입니다(plural 과 select 의 모든 분기 포함). 구문 오류면 원문 그대로입니다. */
        static string mapLiteralText( string_view pattern, TextLiteralMapFunc pfnMap );
    };
} // namespace sw
