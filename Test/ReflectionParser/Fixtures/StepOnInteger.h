// EXPECT-ERROR: Step applies to float32 fields (and float32 math types) only
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct StepOnIntegerType
    {
        REFLECT_BODY();
        PROPERTY( Step = 1, Min = 0, Max = 10 )
        int32 _count{};
    };
} // namespace sw::fixture
