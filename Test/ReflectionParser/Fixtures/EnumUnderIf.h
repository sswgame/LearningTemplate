// EXPECT-ERROR: a reflection macro is under #if
// 파서는 한 구성만 본다 — `#if` 아래의 ENUM 은 그 구성에서 꺼져 있으면 오류 없이 빠지고, 켜져 있으면 구성마다 등록이 달라진다. REFLECT · PROPERTY 와 같은 오류다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
#if 1
    ENUM()
    enum class EnumUnderIfMode : uint8
    {
        First = 0,
        Second
    };
#endif
} // namespace sw::fixture
