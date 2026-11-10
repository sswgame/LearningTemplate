// EXPECT-ERROR: Min/Max apply to integer, floating-point and math fields only
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct RangeOnStringType
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 8, Min = 0, Max = 1 )
        string _name{};
    };
} // namespace sw::fixture
