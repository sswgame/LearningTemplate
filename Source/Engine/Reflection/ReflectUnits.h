/**
 * @file ReflectUnits.h
 * @brief `PROPERTY( Units = … )` 가 받는 단위 표와 단위 사이 변환입니다(헤더 전용 — ReflectionParser 도 같은 표로 철자를 검사합니다).
 * @details 단위는 **저장된 값의 단위**입니다. 인스펙터는 그 단위로 보이고(라디안 · 비율은 도 · 백분율로 바꿔 보인다), 설정 · 콘솔이 다른 단위로
 *          적힌 글(`"150 cm"`)을 받으면 이 표로 저장 단위로 바꿉니다. 같은 차원끼리만 바뀝니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    /** @brief 단위의 차원입니다. 같은 차원끼리만 바꿉니다. */
    enum class ReflectUnitDimension : uint8
    {
        Length,
        Angle,
        Time,
        Ratio,
        Speed,
        AngularSpeed,
        Acceleration,
        Frequency,
        Mass,
        Force,
    };

    /** @brief 단위 하나 — 철자 · 차원 · 기준 단위로의 배율(값 × 배율 = 기준 단위 값). */
    struct ReflectUnit
    {
        const utf8*          _pName;
        ReflectUnitDimension _dimension;
        float64              _toBase;
    };
} // namespace sw

namespace sw
{
    /** @brief `Units = …` 가 받는 철자 전부입니다. 기준 단위는 m · rad · s · ratio · m/s · rad/s · m/s2 · Hz · kg · N 입니다. */
    inline constexpr ReflectUnit kArrReflectUnit[] = {
        {     "mm",       ReflectUnitDimension::Length,                      0.001},
        {     "cm",       ReflectUnitDimension::Length,                       0.01},
        {      "m",       ReflectUnitDimension::Length,                        1.0},
        {     "km",       ReflectUnitDimension::Length,                     1000.0},
        {    "deg",        ReflectUnitDimension::Angle, 0.017453292519943295769237},
        {    "rad",        ReflectUnitDimension::Angle,                        1.0},
        {     "ms",         ReflectUnitDimension::Time,                      0.001},
        {      "s",         ReflectUnitDimension::Time,                        1.0},
        {    "min",         ReflectUnitDimension::Time,                       60.0},
        {      "h",         ReflectUnitDimension::Time,                     3600.0},
        {"percent",        ReflectUnitDimension::Ratio,                       0.01},
        {  "ratio",        ReflectUnitDimension::Ratio,                        1.0},
        {    "m/s",        ReflectUnitDimension::Speed,                        1.0},
        {   "km/h",        ReflectUnitDimension::Speed,    0.277777777777777777778},
        {  "deg/s", ReflectUnitDimension::AngularSpeed, 0.017453292519943295769237},
        {  "rad/s", ReflectUnitDimension::AngularSpeed,                        1.0},
        {   "m/s2", ReflectUnitDimension::Acceleration,                        1.0},
        {     "Hz",    ReflectUnitDimension::Frequency,                        1.0},
        {    "fps",    ReflectUnitDimension::Frequency,                        1.0},
        {      "g",         ReflectUnitDimension::Mass,                      0.001},
        {     "kg",         ReflectUnitDimension::Mass,                        1.0},
        {      "N",        ReflectUnitDimension::Force,                        1.0},
    };

    /** @brief 단위 표를 찾고 값을 바꿉니다. */
    struct ReflectUnitUtil
    {
        /** @brief 철자(대소문자 구분 — `m` 과 `M` 은 다른 단위다)로 단위를 찾습니다. 없으면 nullptr 입니다. */
        static const ReflectUnit* findUnit( const string_view name ) noexcept
        {
            for ( const ReflectUnit& unit : kArrReflectUnit )
            {
                if ( name == unit._pName )
                    return &unit;
            }
            return nullptr;
        }

        /** @brief 받는 철자를 `a, b, c` 꼴 한 줄로 씁니다(오류 메시지용). */
        static string makeUnitList()
        {
            string text;
            for ( const ReflectUnit& unit : kArrReflectUnit )
            {
                if ( text.empty() == false )
                    text += ", ";
                text += unit._pName;
            }
            return text;
        }

        /**
         * @brief @p value 를 @p fromUnit 에서 @p toUnit 으로 바꿉니다.
         * @return 모르는 단위이거나 차원이 다르면 false(@p outValue 는 그대로)
         */
        [[nodiscard]] static bool convert( const float64 value, const string_view fromUnit, const string_view toUnit, float64& outValue ) noexcept
        {
            const ReflectUnit* pFrom = findUnit( fromUnit );
            const ReflectUnit* pTo   = findUnit( toUnit );
            if ( pFrom == nullptr || pTo == nullptr || pFrom->_dimension != pTo->_dimension )
                return false;
            outValue = value * pFrom->_toBase / pTo->_toBase;
            return true;
        }

        /**
         * @brief `"150 cm"` · `"1.5m"` · `"90"` 같은 글을 @p storedUnit 의 값으로 읽습니다. 단위를 적지 않았으면 이미 저장 단위입니다.
         * @return 숫자가 아니거나, 모르는 단위이거나, 차원이 다르면 false
         */
        [[nodiscard]] static bool parseValueInUnit( const string_view text, const string_view storedUnit, float64& outValue )
        {
            // 앞쪽의 숫자(부호 · 소수점 · 지수 포함)와 뒤쪽의 단위로 가른다 — 단위에도 숫자가 든다(`m/s2`).
            const string_view trimmed = StringUtil::trim( text );
            size_t            split   = 0;
            while ( split < trimmed.size() )
            {
                const utf8 character = trimmed[split];
                const bool bDigit    = '0' <= character && character <= '9';
                const bool bExponent = ( character == 'e' || character == 'E' ) && split + 1 < trimmed.size() &&
                                       ( ( '0' <= trimmed[split + 1] && trimmed[split + 1] <= '9' ) || trimmed[split + 1] == '-' || trimmed[split + 1] == '+' );
                if ( bDigit == false && bExponent == false && character != '.' && character != '+' && character != '-' )
                    break;
                ++split;
            }
            float64 number{ 0.0 };
            if ( StringUtil::parseDouble( StringUtil::trim( trimmed.substr( 0, split ) ), number ) == false )
                return false;
            const string_view unitText = StringUtil::trim( trimmed.substr( split ) );
            if ( unitText.empty() )
            {
                outValue = number;
                return true;
            }
            return convert( number, unitText, storedUnit, outValue );
        }
    };
} // namespace sw
