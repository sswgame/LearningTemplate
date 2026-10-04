/**
 * @file Blackboard.h
 * @brief AI 의 메모장 — 이름 → 값(실수 · 정수 · 참거짓 · 위치 · 오브젝트 id)입니다. 행동 트리의 조건 · 작업이 읽고 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 값의 종류입니다. */
    enum class BlackboardValueType : uint8
    {
        None = 0,
        Float,
        Int,
        Bool,
        Vector,
        Object ///< 오브젝트 · 유닛 id(핸들의 묶은 값 — `SlotHandle::packed` 등)
    };

    /** @brief 값 하나입니다. 종류에 맞는 칸만 씁니다. */
    struct SW_GF_API BlackboardValue
    {
        float3              _vector{};
        uint64              _object{ 0 };
        float32             _float{ 0.0f };
        int32               _int{ 0 };
        BlackboardValueType _type{ BlackboardValueType::None };

        /** @brief 비교용 수치입니다(실수 · 정수 · 참거짓 · 오브젝트는 0 이 아니면 1). */
        float32 computeNumeric() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class Blackboard
     * @brief 언리얼 `UBlackboardComponent` 의 자리입니다. 값이 바뀔 때마다 리비전이 올라 행동 트리가 "바뀌었으면 다시 본다" 를 싸게 합니다.
     * @details 이름은 `hashed_string` 이라 리터럴로 부르면 됩니다(`blackboard.setFloat( "Health", 0.5f )`). 없는 이름을 읽으면 기본값입니다.
     */
    class SW_GF_API Blackboard
    {
    public:
        Blackboard();

        void setFloat( const hashed_string& key, float32 value );
        void setInt( const hashed_string& key, int32 value );
        void setBool( const hashed_string& key, bool bValue );
        void setVector( const hashed_string& key, const float3& value );
        void setObject( const hashed_string& key, uint64 objectId );
        /** @brief 값을 지웁니다(없음 — `isSet` 이 false). */
        void clearValue( const hashed_string& key );
        void clear();

        bool                   isSet( const hashed_string& key ) const;
        float32                getFloat( const hashed_string& key, float32 fallback = 0.0f ) const;
        int32                  getInt( const hashed_string& key, int32 fallback = 0 ) const;
        bool                   getBool( const hashed_string& key, bool bFallback = false ) const;
        float3                 getVector( const hashed_string& key, const float3& fallback = float3{} ) const;
        uint64                 getObject( const hashed_string& key, uint64 fallback = 0 ) const;
        const BlackboardValue* findValue( const hashed_string& key ) const;
        uint32                 getRevision() const { return _revision; }

    private:
        BlackboardValue& acquireValue( const hashed_string& key, BlackboardValueType type );

        unordered_map<hashed_string, BlackboardValue> _mapValue;
        uint32                                        _revision;
    };
} // namespace sw
