/**
 * @file ReflectionEnumNames.h
 * @brief ContainerKind / FunctionNetRole 과 식별자 문자열 사이의 변환입니다(정본은 Predefined*.xxx).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    /** @brief 식별자 문자열을 ContainerKind로 파싱합니다. */
    [[nodiscard]] inline bool tryParseContainerKind( string_view spelling, ContainerKind& out ) noexcept
    {
#define REGISTER_CONTAINER_KIND( Name ) \
    if ( spelling == #Name )            \
    {                                   \
        out = ContainerKind::Name;      \
        return true;                    \
    }
#include "Core/Predefined/PredefinedContainerKind.xxx"

#undef REGISTER_CONTAINER_KIND
        return false;
    }

    /** @brief 문자열로 변환합니다. */
    inline const utf8* toString( const ContainerKind kind ) noexcept
    {
        switch ( kind )
        {
#define REGISTER_CONTAINER_KIND( Name ) \
    case ContainerKind::Name:           \
        return #Name;
#include "Core/Predefined/PredefinedContainerKind.xxx"

#undef REGISTER_CONTAINER_KIND
        }
        return constant::reflection::kNone;
    }

    /** @brief .gen.cpp 에 넣을 `sw::ContainerKind::X` 표현식 */
    inline const utf8* toCppExpr( const ContainerKind kind ) noexcept
    {
        switch ( kind )
        {
#define REGISTER_CONTAINER_KIND( Name ) \
    case ContainerKind::Name:           \
        return "sw::ContainerKind::" #Name;
#include "Core/Predefined/PredefinedContainerKind.xxx"

#undef REGISTER_CONTAINER_KIND
        }
        return "sw::ContainerKind::None";
    }

    /** @brief ContainerKind의 기본 래퍼 stem (Map / Vector). */
    inline const utf8* defaultContainerWrapperStem( const ContainerKind kind ) noexcept
    {
        switch ( kind )
        {
            case ContainerKind::None:
                return "";
            case ContainerKind::Map:
                return "Map";
            case ContainerKind::Sequence:
                return "Vector";
        }
        return "Vector";
    }

    /** @brief 바깥 컨테이너에서 한 겹 벗길 멤버 이름 (mapped_type / value_type). */
    inline const utf8* containerElementTypeMember( const ContainerKind outerKind ) noexcept
    {
        return ( outerKind == ContainerKind::Map ) ? constant::reflection::kMappedType : constant::reflection::kValueType;
    }

    /** @brief 식별자 문자열을 FunctionNetRole로 파싱합니다. */
    [[nodiscard]] inline bool tryParseFunctionNetRole( string_view spelling, FunctionNetRole& out ) noexcept
    {
#define REGISTER_FUNCTION_NET_ROLE( Name ) \
    if ( spelling == #Name )               \
    {                                      \
        out = FunctionNetRole::Name;       \
        return true;                       \
    }
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"

#undef REGISTER_FUNCTION_NET_ROLE
        return false;
    }

    /** @brief 문자열로 변환합니다. */
    inline const utf8* toString( const FunctionNetRole role ) noexcept
    {
        switch ( role )
        {
#define REGISTER_FUNCTION_NET_ROLE( Name ) \
    case FunctionNetRole::Name:            \
        return #Name;
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"

#undef REGISTER_FUNCTION_NET_ROLE
        }
        return "Local";
    }

    /** @brief .gen.cpp에 넣을 `sw::FunctionNetRole::X` 표현식. */
    inline const utf8* toCppExpr( const FunctionNetRole role ) noexcept
    {
        switch ( role )
        {
#define REGISTER_FUNCTION_NET_ROLE( Name ) \
    case FunctionNetRole::Name:            \
        return "sw::FunctionNetRole::" #Name;
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"

#undef REGISTER_FUNCTION_NET_ROLE
        }
        return "sw::FunctionNetRole::Local";
    }
} // namespace sw
