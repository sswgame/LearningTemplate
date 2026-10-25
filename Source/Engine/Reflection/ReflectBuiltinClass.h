/**
 * @file ReflectBuiltinClass.h
 * @brief C++ 타입 하나가 `ReflectBuiltinClass` 의 어느 갈래인지를 정하는 유일한 곳입니다.
 * @details 압축 타입의 성분 수는 `PackedValueTraits` 가 알므로 완전한 정의가 필요합니다 — 이 헤더는 `PackedTypes.h` 를 읽는 번역 단위(`ReflectValue.cpp` ·
 *          `UIBindingValue.cpp`)만 include 합니다. 런타임 질의는 `ReflectValueUtil::getBuiltinClass` 가 같은 값을 돌려줍니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Math/PackedTypes.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectValue.h"

namespace sw
{
    /** @brief @p T 의 갈래입니다. 새 갈래의 타입은 여기에 분기를 더한다(분기가 없으면 Other 라 숫자로 읽히지 않는다). */
    template <typename T>
    constexpr ReflectBuiltinClass getReflectBuiltinClass()
    {
        if constexpr ( std::is_same_v<T, bool> )
            return ReflectBuiltinClass::Bool;
        else if constexpr ( std::is_integral_v<T> || std::is_floating_point_v<T> )
            return ReflectBuiltinClass::Number;
        else if constexpr ( std::is_same_v<T, string> || std::is_same_v<T, hashed_string> )
            return ReflectBuiltinClass::Text;
        else if constexpr ( kIsPackedValue<T> )
            return PackedValueTraits<T>::kComponentCount == 1 ? ReflectBuiltinClass::Number : ReflectBuiltinClass::Other;
        else
            return ReflectBuiltinClass::Other;
    }
} // namespace sw
