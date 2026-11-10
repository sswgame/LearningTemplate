// EXPECT-ERROR: MaxCount applies to string, vector and DynamicBitset fields only
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct MaxCountOnScalarType
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 4 )
        int32 _count{};
    };
} // namespace sw::fixture
