/**
 * @file StatBlock.h
 * @brief 이름마다 수치를 두는 집합입니다. 장비 능력치, 스킬 보너스, 보상, 비용처럼 "종류를 코드가 정하지 않는" 숫자 목록에 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

#include <optional>

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 이름 붙은 수치 하나입니다. */
    struct StatValue
    {
        hashed_string _name{};
        float32       _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class StatBlock
     * @brief 넣은 순서를 지키는 작은 이름 → 값 목록입니다(보통 열 개 안팎이라 선형 조회가 맵보다 빠르다).
     * @details XML 은 `<Stats armor="5" speed="-0.1"/>` 처럼 **속성 이름이 곧 능력치 이름**입니다(`MonsterDef::_mapDrop` 과 같은 방식).
     */
    class SW_GF_API StatBlock
    {
    public:
        void setValue( const hashed_string& name, float32 value );
        void addValue( const hashed_string& name, float32 value );
        /** @brief 더하는 값(보너스 · 비용 · 자원)으로 읽습니다. 없는 이름의 기여는 합의 항등원 0 입니다 — 호출부가 다른 기본값을 넣지 않는다. */
        float32 getValue( const hashed_string& name ) const;
        /** @brief 이름이 있을 때만 값입니다. 반드시 있어야 하는 값(지속 시간 · 배율)은 이것으로 읽고 없음을 오류로 다룹니다. */
        [[nodiscard]] std::optional<float32> findValue( const hashed_string& name ) const;
        /** @brief 곱하는 배율로 읽습니다. 없는 이름은 곱의 항등원 1 입니다(보정이 없음) — 호출부가 다른 기본값을 넣지 않는다. */
        float32 getScale( const hashed_string& name ) const;
        bool    hasValue( const hashed_string& name ) const;
        /** @brief @p other 의 값을 더합니다(@p scale 배). */
        void merge( const StatBlock& other, float32 scale = 1.0f );
        void clear() { _listValue.clear(); }
        /** @brief 여러 자원 비용 @p cost 의 모든 이름에서 이 블록의 값이 모자라지 않은지입니다. RTS, 도시 건설, 정복 장르의 여러 자원 지갑에 씁니다. */
        bool canAfford( const StatBlock& cost ) const;
        /** @brief @p cost 를 모두 낼 수 있으면 빼고 true, 하나라도 모자라면 아무것도 빼지 않고 false 입니다. */
        [[nodiscard]] bool trySpend( const StatBlock& cost );

        /** @brief 노드의 속성을 모두 읽어 더합니다. @p pSkipName 은 건너뛸 속성 이름의 쉼표 목록("id,name")입니다. 읽은 수를 돌려줍니다. */
        uint32 loadFromAttributes( const XMLNode& node, const utf8* pSkipName = nullptr );

        const vector<StatValue>& getValues() const { return _listValue; }
        bool                     isEmpty() const { return _listValue.empty(); }
        size_t                   getCount() const { return _listValue.size(); }

        /** @brief 이름 → 값 목록(넣은 순서)을 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 고칠 값 칸입니다. 공개 `findValue`(읽기 전용, 없음은 std::nullopt)와 이름을 달리해 비 const 호출이 이쪽으로 잘못 가지 않게 한다. */
        StatValue* findMutableValue( const hashed_string& name );

        vector<StatValue> _listValue{};
    };
} // namespace sw
