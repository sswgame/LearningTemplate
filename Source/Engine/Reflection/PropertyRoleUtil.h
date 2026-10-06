/**
 * @file PropertyRoleUtil.h
 * @brief 프로퍼티의 역할 플래그(`Replicated` · `RepNotify` · `SaveGame` · `Interp` · `Config`)를 읽는 쪽이 쓰는 도우미입니다.
 * @details 플래그는 `PropertyMetadata` 에 있고, 여기는 그것을 모으고 · 부르고 · 값을 섞는 일입니다. 네트워크 복제 · 세이브 · 시퀀서 값 트랙 ·
 *          설정 바인딩이 같은 질문을 각자 다시 쓰지 않게 한 곳에 둡니다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw
{
    /** @brief 역할 플래그를 읽는 쪽(네트워크 · 세이브 · 시퀀서)의 공통 도우미입니다. */
    struct SW_API PropertyRoleUtil
    {
        // ------------------------------------------------------------------------------
        // 1) 네트워크 — Replicated · RepNotify
        // ------------------------------------------------------------------------------
        /** @brief 상속분까지 `Replicated` 프로퍼티를 기반부터 모읍니다. */
        static void collectReplicatedProperties( const TypeInfo& type, vector<const PropertyInfo*>& outListProperty );
        /**
         * @brief 받은 값으로 프로퍼티를 바꾼 **뒤에** 부릅니다 — `RepNotify` 함수를 부릅니다(받는 쪽 콜백).
         * @param pOldValue 바뀌기 전 값의 자리(프로퍼티의 선언 타입 — 비트필드면 `uint8` 0/1). 함수가 이전 값을 받지 않으면 쓰지 않습니다.
         * @return `RepNotify` 가 없으면 false
         */
        static bool callRepNotify( const PropertyInfo& prop, void* pInstance, const void* pOldValue );

        // ------------------------------------------------------------------------------
        // 2) 시퀀서 — Interp
        // ------------------------------------------------------------------------------
        /** @brief 값 트랙이 섞을 수 있는 타입(실수 · 정수 · float2/3/4 · quaternion)인지 봅니다. 비트필드 · 컨테이너는 아닙니다. */
        static bool isInterpolatable( const PropertyInfo& prop );
        /** @brief 상속분까지 `Interp` 프로퍼티를 기반부터 모읍니다. */
        static void collectInterpProperties( const TypeInfo& type, vector<const PropertyInfo*>& outListProperty );
        /**
         * @brief @p pFrom · @p pTo(프로퍼티 타입의 값) 사이를 @p alpha 로 섞어 인스턴스의 프로퍼티에 씁니다. 정수는 반올림, quaternion 은 slerp 입니다.
         * @return 섞을 수 없는 타입이면 false(아무것도 쓰지 않는다)
         */
        [[nodiscard]] static bool applyInterpolated( const PropertyInfo& prop, void* pInstance, const void* pFrom, const void* pTo, float32 alpha );
    };
} // namespace sw
