/**
 * @file PropertyRoleUtil.h
 * @brief 프로퍼티의 역할 플래그(`Replicated` · `RepNotify` · `SaveGame` · `Interp` · `Config`)를 읽는 쪽이 쓰는 도우미입니다.
 * @details 플래그는 `PropertyMetadata` 에 있고, 여기는 그것을 모으고 · 부르고 · 값을 섞는 일입니다. 네트워크 복제 · 세이브 · 시퀀서 값 트랙 ·
 *          설정 바인딩이 같은 질문을 각자 다시 쓰지 않게 한 곳에 둡니다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/TextAddress.h"

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
         * @param instance 프로퍼티를 가진 객체의 주소입니다.
         * @param oldValue 바뀌기 전 값 자체의 주소(프로퍼티의 선언 타입 — 비트필드면 `uint8` 0/1). 함수가 이전 값을 받지 않으면 쓰지 않습니다.
         * @return `RepNotify` 가 없으면 false
         */
        static bool callRepNotify( const PropertyInfo& prop, MutableObjectAddress instance, ValueAddress oldValue );

        // ------------------------------------------------------------------------------
        // 2) 시퀀서 — Interp
        // ------------------------------------------------------------------------------
        /** @brief 값 트랙이 섞을 수 있는 타입(실수 · 정수 · float2/3/4 · quaternion)인지 봅니다. 비트필드 · 컨테이너는 아닙니다. */
        static bool isInterpolatable( const PropertyInfo& prop );
        /** @brief 상속분까지 `Interp` 프로퍼티를 기반부터 모읍니다. */
        static void collectInterpProperties( const TypeInfo& type, vector<const PropertyInfo*>& outListProperty );
        /**
         * @brief @p from · @p to(프로퍼티 타입의 값 자체의 주소) 사이를 @p alpha 로 섞어 @p instance(객체의 주소)의 프로퍼티에 씁니다. 정수는 반올림, quaternion 은 slerp 입니다.
         * @return 섞을 수 없는 타입이면 false(아무것도 쓰지 않는다)
         */
        [[nodiscard]] static bool applyInterpolated( const PropertyInfo& prop, MutableObjectAddress instance, ValueAddress from, ValueAddress to, float32 alpha );
        /**
         * @brief @p type 의 섞을 수 있는 프로퍼티(상속분까지, `isInterpolatable`)를 모두 @p from · @p to(객체의 주소) 사이에서 @p alpha 로 섞어 @p out 에 씁니다.
         * @details 섞을 수 없는 칸은 건드리지 않는다 — 부르는 쪽이 @p out 을 먼저 채운다(`blendValues` 는 절반에서 바꾼다). 칸을 손으로 나열해 섞으면 새 칸이 블렌드에서 빠진다.
         *          언리얼 PostProcessSettings 블렌드가 칸마다 손으로 쓴 것(생성 코드)과 달리 리플렉션 한 길이다.
         */
        static void blendProperties( const TypeInfo& type, ObjectAddress from, ObjectAddress to, float32 alpha, MutableObjectAddress out );
        /**
         * @brief 리플렉션 값 타입 @p T 두 값을 섞은 값입니다 — 섞을 수 있는 칸은 `blendProperties`, 그 밖의 칸(bool · 열거 · 문자열)은 @p alpha 가 절반 미만이면 from, 아니면 to 다.
         * @details 칸마다 다른 규칙(조리개의 역수 · 한쪽이 없음인 레터박스)은 부르는 쪽이 결과 위에 덮어쓴다.
         */
        template <typename T>
        static T blendValues( const T& from, const T& to, float32 alpha )
        {
            T out = alpha < 0.5f ? from : to;
            blendProperties( requireType( T::StaticType() ), ObjectAddress{ &from }, ObjectAddress{ &to }, alpha, MutableObjectAddress{ &out } );
            return out;
        }

    private:
        /** @brief 등록되지 않은 타입(리플렉션이 서지 않은 실행)은 프로그램 오류라 멈춘다 — 섞지 않은 값을 내지 않는다. */
        static const TypeInfo& requireType( const TypeInfo* pType );
    };
} // namespace sw
