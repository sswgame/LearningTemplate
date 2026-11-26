// EXPECT-ERROR: but it lacks REFLECT()!
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    struct NotReflected
    {
        PROPERTY()
        int32 _value{ 0 };
    };
} // namespace sw::fixture
