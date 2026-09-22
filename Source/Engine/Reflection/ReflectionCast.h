/**
 * @file ReflectionCast.h
 * @brief Reflection-aware cast helpers (HasStaticType / castTo)
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{

    /**
     * @brief Codegen이 REFLECT 타입마다 특수화합니다 (T::StaticType 없이 FQN 조회).
     */
    template <typename T>
    /// @brief codegen이 REFLECT 타입마다 특수화 (T::StaticType 없이 FQN 조회)
    struct ReflectTypeTraits
    {
    };

    template <typename T, typename = void>
    /// @brief T::StaticType()이 없으면 false
    struct HasStaticType : std::false_type
    {
    };

    template <typename T>
    /// @brief T::StaticType()이 있으면 true
    struct HasStaticType<T, std::void_t<decltype( T::StaticType() )>> : std::true_type
    {
    };

    template <typename T>
    inline constexpr bool HasStaticType_v = HasStaticType<T>::value;

    template <typename T, typename = void>
    /// @brief ReflectTypeTraits<T>::StaticType()이 없으면 false
    struct HasReflectStaticType : std::false_type
    {
    };

    template <typename T>
    /// @brief ReflectTypeTraits<T>::StaticType()이 있으면 true
    struct HasReflectStaticType<T, std::void_t<decltype( ReflectTypeTraits<T>::StaticType() )>> : std::true_type
    {
    };

    template <typename T>
    inline constexpr bool HasReflectStaticType_v = HasReflectStaticType<T>::value;

    template <typename T, typename = void>
    /// @brief Has Own Reflect Body (checks if T declares its own REFLECT_BODY rather than slicing to a base class)
    struct HasOwnReflectBody : std::false_type
    {
    };

    template <typename T>
    struct HasOwnReflectBody<T, std::void_t<decltype( std::declval<const T>().swReflectSelf() )>>
        : std::is_same<std::remove_cv_t<std::remove_pointer_t<decltype( std::declval<const T>().swReflectSelf() )>>, T>
    {
    };

    template <typename T>
    inline constexpr bool HasOwnReflectBody_v = HasOwnReflectBody<T>::value;

    template <typename T, typename = void>
    /// @brief getTypeInfo() 멤버 함수 존재 여부
    struct HasGetTypeInfo : std::false_type
    {
    };

    template <typename T>
    struct HasGetTypeInfo<T, std::void_t<decltype( std::declval<const T>().getTypeInfo() )>> : std::true_type
    {
    };

    template <typename T>
    inline constexpr bool HasGetTypeInfo_v = HasGetTypeInfo<T>::value;

    /**
     * @brief TypeInfo 상속 체인을 보고 To*로 캐스트. 실패 시 nullptr.
     * @details 업캐스트(To 가 From 의 기반)는 컴파일 타임에 끝나 런타임 비용이 0 이다. 다운캐스트는
     *          `getTypeInfo()` 가 준 동적 타입의 조상 표를 한 번 본다(`TypeInfo::isDerivedFrom`) — 잠금·할당·
     *          이름 비교·걷기 없음. 표가 없는 타입(레지스트리 밖 사본 등)만 부모 포인터를 걷는다.
     *          예전엔 사슬을 이름으로 두 번 걸었다(한 번은 정적 타입으로 폴백할지 정하려고). 동적
     *          타입이 To 의 자손이 아니면 그보다 위인 정적 타입도 자손일 리 없으므로 그 가드는 답을
     *          바꾸지 않았다. 정적 타입 폴백은 `getTypeInfo()` 가 nullptr 일 때만 한다.
     */
    template <typename To, typename From>
    To* castTo( From* pSrc )
    {
        if constexpr ( std::is_same_v<To, From> || std::is_base_of_v<To, From> )
        {
            return pSrc;
        }
        else
        {
            if ( pSrc == nullptr )
                return nullptr;

            const TypeInfo* pToType = nullptr;
            if constexpr ( HasStaticType_v<To> )
                pToType = To::StaticType();
            else if constexpr ( HasReflectStaticType_v<To> )
                pToType = ReflectTypeTraits<To>::StaticType();
            else if constexpr ( std::is_base_of_v<From, To> )
                return static_cast<To*>( pSrc );
            else
                return nullptr;

            const TypeInfo* pSrcType = nullptr;
            if constexpr ( HasGetTypeInfo_v<From> )
                pSrcType = pSrc->getTypeInfo();
            if ( pSrcType == nullptr )
            {
                if constexpr ( HasStaticType_v<From> )
                    pSrcType = From::StaticType();
                else if constexpr ( HasReflectStaticType_v<From> )
                    pSrcType = ReflectTypeTraits<From>::StaticType();
            }

            if ( pSrcType == nullptr || pSrcType->isDerivedFrom( pToType ) == false )
                return nullptr;
            if constexpr ( std::is_base_of_v<From, To> )
                return static_cast<To*>( pSrc );
            else
                return reinterpret_cast<To*>( pSrc );
        }
    }

    /** @brief TypeInfo 상속 체인을 보고 const To*로 캐스트. 실패 시 nullptr. */
    template <typename To, typename From>
    const To* castTo( const From* pSrc )
    {
        return castTo<To>( const_cast<From*>( pSrc ) );
    }

    /** @brief pSrc가 To 타입이거나 To로부터 파생되었는지 검사합니다. */
    template <typename To, typename From>
    bool isA( const From* pSrc )
    {
        return castTo<To>( pSrc ) != nullptr;
    }

} // namespace sw
