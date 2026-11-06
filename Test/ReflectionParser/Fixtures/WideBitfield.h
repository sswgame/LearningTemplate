// EXPECT-ERROR: has bit width 2
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct WideBits
    {
        REFLECT_BODY();
        PROPERTY()
        uint8 _flags : 2;
    };
} // namespace sw::fixture
