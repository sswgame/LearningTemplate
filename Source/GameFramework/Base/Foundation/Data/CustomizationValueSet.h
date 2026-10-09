/**
 * @file CustomizationValueSet.h
 * @brief 꾸미기 값 묶음 — 매개변수 이름 → 값(슬라이더 · 색 · 고른 항목)입니다. 캐릭터 프리셋과 아이템 인스턴스가 함께 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 꾸미기 값 하나입니다. 종류(슬라이더 · 색 · 고르기 · 부착)는 값이 아니라 스키마(`CustomizationSchema`)가 압니다.
     * @details 슬라이더는 `_number._x`, 색은 `_number` 의 rgba, 고르기 · 부착은 `_option` 을 씁니다.
     */
    struct CustomizationValue
    {
        hashed_string _parameter{};
        hashed_string _option{}; ///< 고르기 · 부착에서 고른 항목 이름
        float4        _number{}; ///< 슬라이더(x) · 색(rgba)

        bool operator==( const CustomizationValue& other ) const
        {
            return _parameter == other._parameter && _option == other._option && _number == other._number;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class CustomizationValueSet
     * @brief 넣은 순서를 지키는 작은 이름 → 꾸미기 값 목록입니다(보통 수십 개 안이라 선형 조회).
     * @details 값은 스키마 범위 · 격자로 맞추기 전의 것일 수 있습니다 — 해석기(`AppearanceResolver`)가 스키마로 정규화한 뒤 씁니다.
     */
    class SW_GF_API CustomizationValueSet
    {
    public:
        void setValue( const CustomizationValue& value );
        void setNumber( const hashed_string& parameter, float32 value );
        void setColor( const hashed_string& parameter, const float4& color );
        void setOption( const hashed_string& parameter, const hashed_string& option );
        /** @brief 값을 지웁니다. 있었으면 true 입니다. */
        [[nodiscard]] bool removeValue( const hashed_string& parameter );
        /** @brief @p other 의 값을 덮어씁니다(같은 이름은 바꾸고 없는 이름은 뒤에 붙인다). */
        void overlay( const CustomizationValueSet& other );
        void clear() { _listValue.clear(); }

        const CustomizationValue* findValue( const hashed_string& parameter ) const;
        /** @brief 같은 이름 · 같은 값의 묶음인가입니다(순서는 보지 않는다). */
        bool                              isEquivalent( const CustomizationValueSet& other ) const;
        const vector<CustomizationValue>& getValues() const { return _listValue; }
        bool                              isEmpty() const { return _listValue.empty(); }
        size_t                            getCount() const { return _listValue.size(); }

    private:
        CustomizationValue* findMutableValue( const hashed_string& parameter );

        vector<CustomizationValue> _listValue{};
    };
} // namespace sw
