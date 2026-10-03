/**
 * @file StatBlock.h
 * @brief 이름 → 수치 묶음입니다 — 장비 능력치 · 스킬 보너스 · 보상 · 비용처럼 "종류를 코드가 정하지 않는" 숫자 목록에 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 이름 붙은 수치 하나입니다. */
    struct StatValue
    {
        hashed_string _name{};
        float32       _value{ 0.0f };
    };

    /**
     * @class StatBlock
     * @brief 넣은 순서를 지키는 작은 이름 → 값 목록입니다(보통 열 개 안팎이라 선형 조회가 맵보다 빠르다).
     * @details XML 은 `<Stats armor="5" speed="-0.1"/>` 처럼 **속성 이름이 곧 능력치 이름**입니다(`MonsterDef::_mapDrop` 과 같은 방식).
     */
    class SW_GF_API StatBlock
    {
    public:
        void    setValue( const hashed_string& name, float32 value );
        void    addValue( const hashed_string& name, float32 value );
        float32 getValue( const hashed_string& name, float32 fallback = 0.0f ) const;
        bool    hasValue( const hashed_string& name ) const;
        /** @brief @p other 의 값을 더합니다(@p scale 배). */
        void merge( const StatBlock& other, float32 scale = 1.0f );
        void clear() { _listValue.clear(); }

        /** @brief 노드의 속성을 모두 읽어 더합니다. @p pSkipName 은 건너뛸 속성 이름(쉼표 목록 — "id,name"). 읽은 수입니다. */
        uint32 loadFromAttributes( const XmlNode& node, const utf8* pSkipName = nullptr );

        const vector<StatValue>& getValues() const { return _listValue; }
        bool                     isEmpty() const { return _listValue.empty(); }
        size_t                   getCount() const { return _listValue.size(); }

    private:
        StatValue* findValue( const hashed_string& name );

        vector<StatValue> _listValue{};
    };
} // namespace sw
