/**
 * @file ReflectAny.h
 * @brief 다형 REFLECT 값용 타입 태그 바이너리 블롭 (SerializeReference 스타일).
 *
 * @note `makeFrom` / `tryGetFrom` 의 **정의는 여기 없다** —
 *       `Engine/Serialization/Core/SerializeReflectAny.cpp` 에 있다. 바이트를 어떻게 채우는지는
 *       BinarySerializer 의 규약이고, Reflection 이 Serialization 을 참조하면 둘이 서로를
 *       참조하는 순환이 된다(그 이유는 해당 파일의 헤더 주석 참고).
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @brief 리플렉트 값을 (typeFqn + BinarySerializer 페이로드)로 저장합니다.
     * @details PROPERTY(Polymorphic)과 함께 씁니다. C# dynamic이 아니라 구체 TypeInfo만 허용합니다.
     */
    struct SW_API ReflectAny
    {
        hashed_string _typeFqn;
        vector<uint8> _bytes;

        /** @brief 비어 있는지 반환합니다. */
        bool empty() const { return _typeFqn.empty() || _bytes.empty(); }

        /** @brief TypeInfo와 값 포인터로 ReflectAny를 만듭니다. */
        static ReflectAny makeFrom( const TypeInfo& info, const void* pValue );
        /** @brief 저장된 값을 대상 TypeInfo로 꺼냅니다. */
        bool tryGetFrom( const TypeInfo& info, void* pOut ) const;

        /** @brief 리플렉트 타입 T에서 ReflectAny를 만듭니다. */
        /** @brief 만듭니다. */
        template <typename T>
        static ReflectAny make( const T& value )
        {
            if constexpr ( HasReflectStaticType<T>::value )
            {
                const TypeInfo* pInfo = ReflectTypeTraits<T>::StaticType();
                if ( pInfo == nullptr )
                    return ReflectAny{};
                return makeFrom( *pInfo, &value );
            }
            else
                return ReflectAny{};
        }

        /** @brief 저장된 값을 T로 꺼냅니다. */
        template <typename T>
        bool tryGet( T& out ) const
        {
            if constexpr ( HasReflectStaticType<T>::value )
            {
                const TypeInfo* pInfo = ReflectTypeTraits<T>::StaticType();
                if ( pInfo == nullptr )
                    return false;
                return tryGetFrom( *pInfo, &out );
            }
            else
                return false;
        }
    };

} // namespace sw
