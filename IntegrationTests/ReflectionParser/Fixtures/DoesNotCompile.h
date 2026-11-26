// EXPECT-ERROR: Parse failed
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct BrokenType
    {
        REFLECT_BODY();
        PROPERTY()
        NoSuchType _value;
    };
} // namespace sw::fixture
