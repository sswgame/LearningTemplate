// EXPECT-ERROR: Step needs both Min and Max
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct StepWithoutRangeType
    {
        REFLECT_BODY();
        PROPERTY( Step = 0.1 )
        float32 _speed{};
    };
} // namespace sw::fixture
