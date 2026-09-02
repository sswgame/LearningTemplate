/**
 * @file ReflectAny.h
 * @brief 다형 REFLECT 값용 타입 태그 바이너리 블롭 (SerializeReference 스타일).
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    struct TypeInfo;

    class SerializeContext;

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
        template <typename T>
        /** @brief 만듭니다. */
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

        template <typename T>
        /** @brief 저장된 값을 T로 꺼냅니다. */
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

    /** @brief SerializeContext에 ReflectAny 텍스트/바이너리 핸들러를 등록합니다. */
    SW_API void registerReflectAnyHandlers( SerializeContext& ctx );
} // namespace sw
