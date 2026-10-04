/**
 * @file PropertyEditCondition.h
 * @brief `PROPERTY( EditCondition = "…" )` 를 풀고 값을 보고 판정합니다 — 인스펙터가 그 프로퍼티를 숨기거나 막을지 정하는 자리입니다.
 * @details 식은 넷 중 하나입니다(공백 자유). `name` — 그 프로퍼티(bool · 정수)가 참일 때, `!name` — 거짓일 때, `name == Value` ·
 *          `name != Value` — 열거형(이름 · 숫자) · 정수가 같을 때 · 다를 때. `name` 은 같은 타입의 프로퍼티(기반 포함) 이름입니다.
 *          에디터 메타데이터라 Shipping 에는 식이 없고 판정은 늘 "켜짐" 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct EnumInfo;
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw
{
    /** @brief 인스펙터에서 프로퍼티를 어떻게 보일지입니다. */
    enum class PropertyEditState : uint8
    {
        Enabled,  ///< 고칠 수 있다(조건이 없거나 참)
        Disabled, ///< 보이되 막는다(조건이 거짓)
        Hidden,   ///< 숨긴다(조건이 거짓이고 `EditConditionHides`)
    };

    /** @brief 풀어 둔 조건식 하나입니다. */
    struct PropertyEditConditionExpr
    {
        const PropertyInfo* _pSubject{ nullptr }; ///< 조건이 보는 프로퍼티
        const EnumInfo*     _pEnum{ nullptr };    ///< 그 프로퍼티가 열거형이면 그 표
        int64               _compareValue{ 0 };   ///< `==` · `!=` 의 오른쪽 값
        bool                _bNegate{ false };    ///< `!name` · `!=`
        bool                _bCompare{ false };   ///< `==` · `!=` 꼴
    };
} // namespace sw

namespace sw
{
    /** @brief EditCondition 식을 풀고 판정합니다. */
    struct SW_API PropertyEditCondition
    {
        /**
         * @brief @p prop 의 EditCondition 식을 @p type(기반 포함)에 대해 풉니다.
         * @param outError 풀지 못한 이유(로그 · 시험용). 식이 없으면 비어 있고 true 입니다.
         * @return 식이 없거나 풀었으면 true. 이름이 없거나 · 열거자가 없거나 · 꼴이 틀리면 false
         */
        [[nodiscard]] static bool parse( const TypeInfo& type, const PropertyInfo& prop, PropertyEditConditionExpr& outExpr, string& outError );
        /** @brief 풀어 둔 식을 인스턴스 값으로 판정합니다. 보는 프로퍼티가 없으면(식 없음) 참입니다. */
        static bool evaluate( const PropertyEditConditionExpr& expr, const void* pInstance );
        /** @brief 인스펙터가 쓰는 한 번에 — 풀고 판정해 보일 상태를 돌려줍니다. 풀지 못하면 `Enabled`(막지 않는다)입니다. */
        static PropertyEditState getEditState( const TypeInfo& type, const PropertyInfo& prop, const void* pInstance );
    };
} // namespace sw
