// EXPECT-ERROR: value is not a whole number of at least 1
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct ZeroMaxCountType
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 0 )
        string _name{};
    };
} // namespace sw::fixture
