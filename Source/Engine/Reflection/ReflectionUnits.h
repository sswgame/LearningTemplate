/**
 * @file ReflectionUnits.h
 * @brief `PROPERTY( Units = … )` 가 받는 단위 표와 단위 사이 변환입니다(헤더 전용 — ReflectionParser 도 같은 표로 철자를 검사합니다).
 * @details 단위는 **저장된 값의 단위**입니다. 인스펙터는 그 단위로 보이고(라디안 · 비율은 도 · 백분율로 바꿔 보인다), 설정 · 콘솔이 다른 단위로
 *          적힌 글(`"150 cm"`)을 받으면 이 표로 저장 단위로 바꿉니다. 같은 차원끼리만 바뀝니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
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
        Torque,
        Impulse,
        Temperature, ///< 색온도 · 온도(K) — 화이트 밸런스(`PostProcessValues::_whiteTemperature`)
    };

    /**
     * @brief `PROPERTY( Units = … )` 의 단위입니다 — 프로퍼티 메타데이터가 글이 아니라 이 값으로 든다(`PropertyMetadata::_unit`). None 은 단위 없음입니다.
     * @details 예전에는 검증한 철자를 커스텀 메타 글로 실어 인스펙터가 `== "rad"` 로 다시 풀었다. 열거자 순서는 아래 표(`kArrReflectUnit`)의 순서다.
     */
    enum class ReflectUnitKind : uint8
    {
        None,
        Millimeter,
        Centimeter,
        Meter,
        Kilometer,
        Degree,
        Radian,
        Millisecond,
        Second,
        Minute,
        Hour,
        Percent,
        Ratio,
        MeterPerSecond,
        KilometerPerHour,
        DegreePerSecond,
        RadianPerSecond,
        MeterPerSecondSquared,
        Hertz,
        FramesPerSecond,
        Gram,
        Kilogram,
        Newton,
        NewtonMeter,
        NewtonSecond,
        Kelvin,
    };

    /** @brief 단위 하나 — 종류 · 철자 · 차원 · 기준 단위로의 배율(값 × 배율 = 기준 단위 값). */
    struct ReflectUnit
    {
        ReflectUnitKind      _kind;
        const utf8*          _pName;
        ReflectUnitDimension _dimension;
        float64              _toBase;

        // 단위 표를 찾고 값을 바꿉니다.
        /** @brief 철자(대소문자 구분 — `m` 과 `M` 은 다른 단위다)로 단위를 찾습니다. 없으면 nullptr 입니다. */
        static const ReflectUnit* findUnit( const string_view name ) noexcept;
        /** @brief 종류로 단위를 찾습니다. None 이면 nullptr 입니다. */
        static const ReflectUnit* findUnit( ReflectUnitKind kind ) noexcept;
        /** @brief 철자의 종류입니다(없으면 None). 생성 코드가 상수 문맥에서 불러 철자 오타를 컴파일 오류로 만든다. */
        static constexpr ReflectUnitKind getKindOf( string_view name ) noexcept;

        /** @brief 받는 철자를 `a, b, c` 꼴 한 줄로 씁니다(오류 메시지용). */
        static string makeUnitList();

        /**
         * @brief @p value 를 @p fromUnit 에서 @p toUnit 으로 바꿉니다.
         * @return 모르는 단위이거나 차원이 다르면 false(@p outValue 는 그대로)
         */
        [[nodiscard]] static bool convert( const float64 value, const string_view fromUnit, const string_view toUnit, float64& outValue ) noexcept;

        /**
         * @brief `"150 cm"` · `"1.5m"` · `"90"` 같은 글을 @p storedUnit 의 값으로 읽습니다. 단위를 적지 않았으면 이미 저장 단위입니다.
         * @return 숫자가 아니거나, 모르는 단위이거나, 차원이 다르면 false
         */
        [[nodiscard]] static bool parseValueInUnit( const string_view text, const string_view storedUnit, float64& outValue );
    };
} // namespace sw

namespace sw
{
    /** @brief `Units = …` 가 받는 철자 전부입니다. 기준 단위는 m · rad · s · ratio · m/s · rad/s · m/s2 · Hz · kg · N · N*m · N*s · K 입니다. */
    inline constexpr ReflectUnit kArrReflectUnit[] = {
        {           ReflectUnitKind::Millimeter,      "mm",       ReflectUnitDimension::Length,                   0.001},
        {           ReflectUnitKind::Centimeter,      "cm",       ReflectUnitDimension::Length,                    0.01},
        {                ReflectUnitKind::Meter,       "m",       ReflectUnitDimension::Length,                     1.0},
        {            ReflectUnitKind::Kilometer,      "km",       ReflectUnitDimension::Length,                  1000.0},
        {               ReflectUnitKind::Degree,     "deg",        ReflectUnitDimension::Angle, MathUtil::kPi64 / 180.0},
        {               ReflectUnitKind::Radian,     "rad",        ReflectUnitDimension::Angle,                     1.0},
        {          ReflectUnitKind::Millisecond,      "ms",         ReflectUnitDimension::Time,                   0.001},
        {               ReflectUnitKind::Second,       "s",         ReflectUnitDimension::Time,                     1.0},
        {               ReflectUnitKind::Minute,     "min",         ReflectUnitDimension::Time,                    60.0},
        {                 ReflectUnitKind::Hour,       "h",         ReflectUnitDimension::Time,                  3600.0},
        {              ReflectUnitKind::Percent, "percent",        ReflectUnitDimension::Ratio,                    0.01},
        {                ReflectUnitKind::Ratio,   "ratio",        ReflectUnitDimension::Ratio,                     1.0},
        {       ReflectUnitKind::MeterPerSecond,     "m/s",        ReflectUnitDimension::Speed,                     1.0},
        {     ReflectUnitKind::KilometerPerHour,    "km/h",        ReflectUnitDimension::Speed, 0.277777777777777777778},
        {      ReflectUnitKind::DegreePerSecond,   "deg/s", ReflectUnitDimension::AngularSpeed, MathUtil::kPi64 / 180.0},
        {      ReflectUnitKind::RadianPerSecond,   "rad/s", ReflectUnitDimension::AngularSpeed,                     1.0},
        {ReflectUnitKind::MeterPerSecondSquared,    "m/s2", ReflectUnitDimension::Acceleration,                     1.0},
        {                ReflectUnitKind::Hertz,      "Hz",    ReflectUnitDimension::Frequency,                     1.0},
        {      ReflectUnitKind::FramesPerSecond,     "fps",    ReflectUnitDimension::Frequency,                     1.0},
        {                 ReflectUnitKind::Gram,       "g",         ReflectUnitDimension::Mass,                   0.001},
        {             ReflectUnitKind::Kilogram,      "kg",         ReflectUnitDimension::Mass,                     1.0},
        {               ReflectUnitKind::Newton,       "N",        ReflectUnitDimension::Force,                     1.0},
        {          ReflectUnitKind::NewtonMeter,     "N*m",       ReflectUnitDimension::Torque,                     1.0},
        {         ReflectUnitKind::NewtonSecond,     "N*s",      ReflectUnitDimension::Impulse,                     1.0},
        {               ReflectUnitKind::Kelvin,       "K",  ReflectUnitDimension::Temperature,                     1.0},
    };

    constexpr bool isReflectUnitTableOrdered() noexcept
    {
        for ( size_t index = 0; index < std::size( kArrReflectUnit ); ++index )
        {
            if ( static_cast<size_t>( kArrReflectUnit[index]._kind ) != index + 1 )
                return false;
        }
        return true;
    }
    static_assert( isReflectUnitTableOrdered(), "kArrReflectUnit must list ReflectUnitKind in enum order (findUnit( kind ) indexes it)" );

    constexpr ReflectUnitKind ReflectUnit::getKindOf( const string_view name ) noexcept
    {
        for ( const ReflectUnit& unit : kArrReflectUnit )
        {
            if ( name == unit._pName )
                return unit._kind;
        }
        return ReflectUnitKind::None;
    }

    inline const ReflectUnit* ReflectUnit::findUnit( const ReflectUnitKind kind ) noexcept
    {
        return kind == ReflectUnitKind::None ? nullptr : &kArrReflectUnit[static_cast<size_t>( kind ) - 1];
    }

    inline const ReflectUnit* ReflectUnit::findUnit( const string_view name ) noexcept
    {
        for ( const ReflectUnit& unit : kArrReflectUnit )
        {
            if ( name == unit._pName )
                return &unit;
        }
        return nullptr;
    }

    inline string ReflectUnit::makeUnitList()
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

    inline bool ReflectUnit::convert( const float64 value, const string_view fromUnit, const string_view toUnit, float64& outValue ) noexcept
    {
        const ReflectUnit* pFrom              = findUnit( fromUnit );
        const ReflectUnit* pTo                = findUnit( toUnit );
        const bool         bIncompatibleUnits = pFrom == nullptr || pTo == nullptr || pFrom->_dimension != pTo->_dimension;
        if ( bIncompatibleUnits )
            return false;
        outValue = value * pFrom->_toBase / pTo->_toBase;
        return true;
    }

    inline bool ReflectUnit::parseValueInUnit( const string_view text, const string_view storedUnit, float64& outValue )
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
            const bool bNumberEnded = bDigit == false && bExponent == false && character != '.' && character != '+' && character != '-';
            if ( bNumberEnded )
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

} // namespace sw
