// EXPECT-ERROR: lacks REFLECT_BODY()
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct NoBody
    {
        PROPERTY()
        int32 _value{ 0 };
    };
} // namespace sw::fixture
