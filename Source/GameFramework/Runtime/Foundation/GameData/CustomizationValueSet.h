/**
 * @file CustomizationValueSet.h
 * @brief 꾸미기 값 집합입니다. 매개변수 이름마다 값(슬라이더, 색, 고른 항목)을 둡니다. 캐릭터 프리셋과 아이템 인스턴스가 함께 씁니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 꾸미기 값 하나입니다. 종류(슬라이더, 색, 고르기, 부착)는 값이 아니라 스키마(`CustomizationSchema`)가 압니다.
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
     * @details 값은 스키마의 범위와 간격에 맞추기 전의 것일 수 있습니다. 해석기(`ResolvedAppearance`)가 스키마로 정규화한 뒤 씁니다.
     *          **스키마 판**(`getSchemaVersion`): 값을 쓴 스키마의 판입니다. 세이브 · 공유 코드가 함께 싣고, 스키마가 바뀐 뒤 읽으면 `CustomizationSchemaDef::migrate` 가
     *          판을 차례로 올린다(이름 바꾸기 · 채널 나누기 · 팔레트 번호 → 색). 0 은 아직 판을 찍지 않은 값 — 이 실행이 읽은 스키마(콘텐츠 · 코드)로 만든 값이라 지금 판으로 읽는다.
     *          스키마를 아는 자리(`applyValue` · `normalize` · `migrate` · 공유 코드 읽기)가 판을 찍는다.
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
        /** @brief @p other 의 값을 덮어씁니다(같은 이름은 바꾸고 없는 이름은 뒤에 붙인다). 비어 있던 집합은 @p other 의 판을 받습니다. */
        void overlay( const CustomizationValueSet& other );
        void clear()
        {
            _listValue.clear();
            _schemaVersion = kUnversionedSchemaVersion;
        }
        /** @brief 값을 쓴 스키마의 판입니다. 0(`kUnversionedSchemaVersion`)은 판을 찍지 않은 값입니다. */
        uint32 getSchemaVersion() const { return _schemaVersion; }
        void   setSchemaVersion( uint32 version ) { _schemaVersion = version; }

        static constexpr uint32 kUnversionedSchemaVersion = 0;
        /** @brief 판을 두기 전에 저장한 값(옛 세이브 · 옛 공유 코드)의 판입니다 — 판을 적지 않은 스키마의 판과 같다. */
        static constexpr uint32 kFirstSchemaVersion = 1;

        const CustomizationValue* findValue( const hashed_string& parameter ) const;
        /** @brief 판과 이름 · 값이 모두 같은 집합인지입니다. 순서는 보지 않고, 판을 찍지 않은 쪽(0 — 지금 판)은 어느 판과도 맞는 것으로 봅니다. */
        bool                              isEquivalent( const CustomizationValueSet& other ) const;
        const vector<CustomizationValue>& getValues() const { return _listValue; }
        bool                              isEmpty() const { return _listValue.empty(); }
        size_t                            getCount() const { return _listValue.size(); }

    private:
        CustomizationValue* findMutableValue( const hashed_string& parameter );

    private:
        vector<CustomizationValue> _listValue{};
        uint32                     _schemaVersion{ kUnversionedSchemaVersion };
    };
} // namespace sw
