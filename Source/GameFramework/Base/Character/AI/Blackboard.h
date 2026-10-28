/**
 * @file Blackboard.h
 * @brief AI 의 메모장입니다. 이름으로 값(실수, 정수, 참거짓, 위치, 오브젝트 id)을 저장하고, 행동 트리의 조건과 작업이 읽고 씁니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

#include <optional>

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
        Object ///< 오브젝트나 유닛의 id. 핸들을 하나로 묶은 값이다(`SlotHandle::packed` 등)
    };

    /** @brief 값 하나입니다. 종류에 맞는 필드만 씁니다. */
    struct SW_GF_API BlackboardValue
    {
        float3              _vector{};
        uint64              _object{ 0 };
        float32             _float{ 0.0f };
        int32               _int{ 0 };
        BlackboardValueType _type{ BlackboardValueType::None };

        /** @brief 비교에 쓰는 수치입니다. 참거짓과 오브젝트는 0 이 아니면 1 입니다. */
        float32 computeNumeric() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class Blackboard
     * @brief 언리얼 `UBlackboardComponent` 의 자리입니다. 값이 바뀔 때마다 리비전이 올라 행동 트리가 "바뀌었으면 다시 본다" 를 싸게 합니다.
     * @details 이름은 `hashed_string` 이라 리터럴로 부르면 됩니다(`blackboard.setFloat( "Health", 0.5f )`).
     *          키는 실행 중에 행동 트리가 만들므로 핸들로 바꿀 수 없다. 그래서 읽기는 `std::optional` 을 돌려주고, 없는(또는 종류가 맞지 않는) 값은
     *          비어 있다 — 부르는 쪽이 없음을 직접 처리한다(언리얼 `GetValueAsFloat` 처럼 0 을 돌려주지 않는다, 데이터 주도 계획 7.3).
     */
    class SW_GF_API Blackboard
    {
    public:
        Blackboard();

        void setFloat( const hashed_string& key, float32 value );
        void setInt( const hashed_string& key, int32 value );
        void setBool( const hashed_string& key, bool bValue );
        void setVector( const hashed_string& key, const float3& value );
        void setObject( const hashed_string& key, uint64 objectID );
        /** @brief 값을 지웁니다. 지운 뒤에는 `isSet` 이 false 입니다. */
        void clearValue( const hashed_string& key );
        void clear();

        bool isSet( const hashed_string& key ) const;
        /** @brief 수치로 읽습니다(실수 · 정수 · 참거짓 · 오브젝트). 없으면 비어 있습니다. */
        [[nodiscard]] std::optional<float32> getFloat( const hashed_string& key ) const;
        /** @brief 정수로 읽습니다(정수 · 참거짓 · 실수의 정수부). 없거나 다른 종류면 비어 있습니다. */
        [[nodiscard]] std::optional<int32> getInt( const hashed_string& key ) const;
        /** @brief 참거짓으로 읽습니다(수치가 0 이 아니면 참). 없으면 비어 있습니다. */
        [[nodiscard]] std::optional<bool> getBool( const hashed_string& key ) const;
        /** @brief 위치를 읽습니다. 없거나 다른 종류면 비어 있습니다. */
        [[nodiscard]] std::optional<float3> getVector( const hashed_string& key ) const;
        /** @brief 오브젝트 id 를 읽습니다. 없거나 다른 종류면 비어 있습니다. */
        [[nodiscard]] std::optional<uint64> getObject( const hashed_string& key ) const;
        const BlackboardValue*              findValue( const hashed_string& key ) const;
        uint32                              getRevision() const { return _revision; }

    private:
        BlackboardValue& acquireValue( const hashed_string& key, BlackboardValueType type );

        unordered_map<hashed_string, BlackboardValue> _mapValue;
        uint32                                        _revision;
    };
} // namespace sw
