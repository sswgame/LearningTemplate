// EXPECT-ERROR: Step must be greater than zero
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct NonPositiveStepType
    {
        REFLECT_BODY();
        PROPERTY( Step = 0, Min = 0, Max = 1 )
        float32 _speed{};
    };
} // namespace sw::fixture
