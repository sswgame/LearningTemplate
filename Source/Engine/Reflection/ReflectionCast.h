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

    template <typename T, typename = void>
    /// @brief findCachedTypeInfo() 멤버 함수 존재 여부 — 가상 getTypeInfo() 앞에 이름 캐시를 먼저 보는 타입(Component)
    struct HasFindCachedTypeInfo : std::false_type
    {
    };

    template <typename T>
    struct HasFindCachedTypeInfo<T, std::void_t<decltype( std::declval<const T>().findCachedTypeInfo() )>> : std::true_type
    {
    };

    template <typename T>
    inline constexpr bool HasFindCachedTypeInfo_v = HasFindCachedTypeInfo<T>::value;

    /**
     * @brief T 의 정적 TypeInfo — 코드젠의 `StaticType()` 이나 `ReflectTypeTraits` 특수화가 있으면 그것, 없으면 nullptr.
     * @details `castTo` 가 To 쪽을 이것으로 푼다. 컴포넌트 목록을 도는 조회 루프는 이것을 **루프 밖에서 한 번** 구해
     *          `castTo( pSrc, pToType )` 에 넘긴다 — 세대 검사 캐시 조회 하나(약 4 ns)를 컴포넌트마다 내지 않는다.
     */
    template <typename T>
    const TypeInfo* findStaticType()
    {
        if constexpr ( HasStaticType_v<T> )
            return T::StaticType();
        else if constexpr ( HasReflectStaticType_v<T> )
            return ReflectTypeTraits<T>::StaticType();
        else
            return nullptr;
    }

    /**
     * @brief To 의 TypeInfo 를 밖에서 받아 To*로 캐스트. 실패 시 nullptr. 조회 루프용.
     * @details 업캐스트(To 가 From 의 기반)는 컴파일 타임에 끝나 런타임 비용이 0 이다. 다운캐스트는 `getTypeInfo()` 가
     *          준 동적 타입의 조상 표를 한 번 본다(`TypeInfo::isDerivedFrom`) — 잠금·할당·이름 비교·걷기 없음. To 에
     *          정적 타입이 없으면 pToType 은 무시되고, To 가 From 의 파생이면 검사 없이 내려간다(리플렉션 밖 타입의
     *          옛 규칙). 정적 타입 폴백은 `getTypeInfo()` 가 nullptr 일 때만 한다 — 동적 타입이 To 의 자손이 아니면
     *          그보다 위인 정적 타입도 자손일 리 없다.
     */
    template <typename To, typename From>
    To* castTo( From* pSrc, const TypeInfo* pToType )
    {
        if constexpr ( std::is_same_v<To, From> || std::is_base_of_v<To, From> )
        {
            (void)pToType;
            return pSrc;
        }
        else if constexpr ( HasStaticType_v<To> == false && HasReflectStaticType_v<To> == false )
        {
            (void)pToType;
            if constexpr ( std::is_base_of_v<From, To> )
                return static_cast<To*>( pSrc );
            else
                return nullptr;
        }
        else
        {
            if ( pSrc == nullptr || pToType == nullptr )
                return nullptr;

            // 동적 타입 — 이름 캐시가 있으면 가상 호출 없이(Component), 아니면 가상 getTypeInfo().
            const TypeInfo* pSrcType = nullptr;
            if constexpr ( HasFindCachedTypeInfo_v<From> )
                pSrcType = pSrc->findCachedTypeInfo();
            else if constexpr ( HasGetTypeInfo_v<From> )
                pSrcType = pSrc->getTypeInfo();
            if ( pSrcType == nullptr )
                pSrcType = findStaticType<From>();

            if ( pSrcType == nullptr || pSrcType->isDerivedFrom( pToType ) == false )
                return nullptr;
            if constexpr ( std::is_base_of_v<From, To> )
                return static_cast<To*>( pSrc );
            else
                return reinterpret_cast<To*>( pSrc );
        }
    }

    /** @brief TypeInfo 상속 체인을 보고 To*로 캐스트. 실패 시 nullptr. 규칙은 위의 판과 같다. */
    template <typename To, typename From>
    To* castTo( From* pSrc )
    {
        if constexpr ( std::is_same_v<To, From> || std::is_base_of_v<To, From> )
            return pSrc;
        else
            return castTo<To>( pSrc, findStaticType<To>() );
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
