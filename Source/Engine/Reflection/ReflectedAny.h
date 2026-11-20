/**
 * @file ReflectedAny.h
 * @brief 다형 REFLECT 값을 담는 타입 태그 바이너리 블롭입니다(SerializeReference 스타일).
 *
 * @note `makeFrom` / `tryGetFrom` 의 **정의는 여기 없습니다.**
 *       `Engine/Serialization/Serializer/SerializeReflectAny.cpp` 에 있습니다. 바이트를 어떻게 채우는지는
 *       BinarySerializer 의 규약이고, Reflection 이 Serialization 을 참조하면 둘이 서로를
 *       참조하는 순환이 됩니다(이유는 그 파일의 머리말 참고).
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @brief 리플렉트 값을 (typeFqn + BinarySerializer 페이로드)로 저장합니다.
     * @details C# dynamic이 아니라 구체 TypeInfo만 허용합니다.
     */
    struct SW_API ReflectedAny
    {
        hashed_string _typeFqn;
        vector<uint8> _bytes;

        /** @brief 비어 있는지 반환합니다. */
        bool empty() const { return _typeFqn.empty() || _bytes.empty(); }

        /** @brief TypeInfo와 값 포인터로 ReflectAny를 만듭니다. */
        static ReflectedAny makeFrom( const TypeInfo& info, const void* pValue );
        /** @brief 저장된 값을 대상 TypeInfo로 꺼냅니다. */
        [[nodiscard]] bool tryGetFrom( const TypeInfo& info, void* pOut ) const;

        /**
         * @brief 리플렉트 타입 T 의 값으로 ReflectedAny 를 만듭니다. T 는 `REFLECT_BODY()` 를 선언한 형식이어야 합니다(컴파일 오류).
         * @details 리플렉트 형식이 아닌 T 를 빈 값으로 조용히 넘기지 않는다 — 형식이 틀리면 그 자리에서 멈춘다.
         */
        template <typename T>
        static ReflectedAny make( const T& value )
        {
            static_assert( HasStaticType_v<T>, "ReflectedAny::make<T> needs a REFLECT type with REFLECT_BODY()" );
            const TypeInfo* pInfo = T::StaticType();
            if ( pInfo == nullptr )
                return ReflectedAny{};
            return makeFrom( *pInfo, &value );
        }

        /** @brief 저장된 값을 T 로 꺼냅니다. T 는 `REFLECT_BODY()` 를 선언한 형식이어야 합니다(컴파일 오류). */
        template <typename T>
        [[nodiscard]] bool tryGet( T& outValue ) const
        {
            static_assert( HasStaticType_v<T>, "ReflectedAny::tryGet<T> needs a REFLECT type with REFLECT_BODY()" );
            const TypeInfo* pInfo = T::StaticType();
            if ( pInfo == nullptr )
                return false;
            return tryGetFrom( *pInfo, &outValue );
        }
    };

} // namespace sw
