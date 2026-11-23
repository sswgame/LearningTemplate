/**
 * @file ReflectionAnnotationKeys.h
 * @brief 리플렉션 어노테이션 인자 키를 편집기에 알리는 선언입니다.
 * @details 키는 정본 표 `Core/Predefined/PredefinedAnnotationField.xxx`(파서가 읽는 그 표)를 펼쳐 만든다 — 손으로 맞추던 사본이 표와
 *          어긋나 있었다(TickMode · RemovedField · RemovedType · Hidden · CatalogKeyTag 빠짐). 단위는 `ReflectionUnits.h` 의 철자, 넷 역할은
 *          `PredefinedFunctionNetRole.xxx`, 컨테이너 종류는 `PredefinedContainerKind.xxx` 다.
 *          편집기에서만 include 됩니다(`ReflectionIntellisense.h`). 빌드와 ReflectionParser 는 이 파일을 읽지 않습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::reflect::attr
{
    /** @brief 키 하나. `Key = 값` 의 값은 무엇이든 받습니다(값 형식은 ReflectionParser 가 검사합니다). */
    struct AnnotationKey
    {
        /** @brief `Key = 값` 을 식으로 받습니다. */
        template <typename T>
        constexpr const AnnotationKey& operator=( T&& ) const noexcept
        {
            return *this;
        }
    };
} // namespace sw::reflect::attr

// 표 한 줄 → 스코프 하나의 키. `SW_ATTR_IN_<Scope>` 다섯을 블록마다 고른 스코프만 펼치게 다시 정의한다.
#define REGISTER_ANNOTATION_FIELD( Scope, Key, Member, Emit )        SW_ATTR_IN_##Scope( Key )
#define REGISTER_ANNOTATION_FIELD_FN( Scope, Key, Member, Fn, Emit ) SW_ATTR_IN_##Scope( Key )
#define SW_ATTR_MEMBER( Key )                                        static constexpr AnnotationKey Key{};
#define SW_ATTR_NAME( Key )                                          #Key,
#define SW_ATTR_SKIP( Key )

namespace sw::reflect::attr
{
#define SW_ATTR_IN_Reflect    SW_ATTR_MEMBER
#define SW_ATTR_IN_Enum       SW_ATTR_SKIP
#define SW_ATTR_IN_Enumerator SW_ATTR_SKIP
#define SW_ATTR_IN_Property   SW_ATTR_SKIP
#define SW_ATTR_IN_Function   SW_ATTR_SKIP
    /** @brief `REFLECT( … )` 의 키입니다. */
    struct ReflectKeys
    {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
    };
#undef SW_ATTR_IN_Reflect
#define SW_ATTR_IN_Reflect SW_ATTR_NAME
    inline constexpr const utf8* kArrReflectKey[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        nullptr,
    };
#undef SW_ATTR_IN_Reflect
#define SW_ATTR_IN_Reflect SW_ATTR_SKIP

#undef SW_ATTR_IN_Enum
#define SW_ATTR_IN_Enum SW_ATTR_MEMBER
    /** @brief `ENUM( … )` 의 키입니다. */
    struct EnumKeys
    {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
    };
#undef SW_ATTR_IN_Enum
#define SW_ATTR_IN_Enum SW_ATTR_NAME
    inline constexpr const utf8* kArrEnumKey[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        nullptr,
    };
#undef SW_ATTR_IN_Enum
#define SW_ATTR_IN_Enum SW_ATTR_SKIP

#undef SW_ATTR_IN_Property
#define SW_ATTR_IN_Property SW_ATTR_MEMBER
    /** @brief `PROPERTY( … )` 의 키입니다. 단위 철자(`Units = mm`)도 멤버로 둔다 — 값 자리에서 이름으로 풀린다(단위 표는 ReflectionUnits.h 의 kArrReflectUnit). */
    struct PropertyKeys
    {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        static constexpr AnnotationKey mm{};
        static constexpr AnnotationKey cm{};
        static constexpr AnnotationKey m{};
        static constexpr AnnotationKey km{};
        static constexpr AnnotationKey deg{};
        static constexpr AnnotationKey rad{};
        static constexpr AnnotationKey ms{};
        static constexpr AnnotationKey s{};
        static constexpr AnnotationKey min{};
        static constexpr AnnotationKey h{};
        static constexpr AnnotationKey percent{};
        static constexpr AnnotationKey ratio{};
        static constexpr AnnotationKey Hz{};
        static constexpr AnnotationKey fps{};
        static constexpr AnnotationKey g{};
        static constexpr AnnotationKey kg{};
        static constexpr AnnotationKey N{};
    };
#undef SW_ATTR_IN_Property
#define SW_ATTR_IN_Property SW_ATTR_NAME
    inline constexpr const utf8* kArrPropertyKey[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        nullptr,
    };
#undef SW_ATTR_IN_Property
#define SW_ATTR_IN_Property SW_ATTR_SKIP

#undef SW_ATTR_IN_Function
#define SW_ATTR_IN_Function SW_ATTR_MEMBER
    /** @brief `FUNCTION( … )` 의 키와 넷 역할 이름(단독 토큰 `Server` 등)입니다. 표의 `NetRole` 줄도 멤버가 되지만 소스에는 역할 이름을 적는다. */
    struct FunctionKeys
    {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
#define REGISTER_FUNCTION_NET_ROLE( Name ) static constexpr AnnotationKey Name{};
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"
#undef REGISTER_FUNCTION_NET_ROLE
    };
#undef SW_ATTR_IN_Function
#define SW_ATTR_IN_Function SW_ATTR_NAME
    inline constexpr const utf8* kArrFunctionKey[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
#define REGISTER_FUNCTION_NET_ROLE( Name ) #Name,
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"
#undef REGISTER_FUNCTION_NET_ROLE
        nullptr,
    };
#undef SW_ATTR_IN_Function

#undef SW_ATTR_IN_Reflect
#undef SW_ATTR_IN_Enum
#undef SW_ATTR_IN_Enumerator
#undef SW_ATTR_IN_Property
} // namespace sw::reflect::attr

#undef REGISTER_ANNOTATION_FIELD
#undef REGISTER_ANNOTATION_FIELD_FN
#undef SW_ATTR_MEMBER
#undef SW_ATTR_NAME
#undef SW_ATTR_SKIP

namespace sw::reflect::attr
{
    /** @brief `REFLECT_CONTAINER( 종류[, 래퍼] )` 의 종류입니다. 둘째 인자는 래퍼 이름(`Deque` → DequeWrapper)입니다. */
    struct ContainerKeys
    {
#define REGISTER_CONTAINER_KIND( Name ) static constexpr AnnotationKey Name{};
#include "Core/Predefined/PredefinedContainerKind.xxx"
#undef REGISTER_CONTAINER_KIND
    };
} // namespace sw::reflect::attr
