// EXPECT-ERROR: MaxCount constant 'kNotDeclared' is not declared
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct BadCount
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = kNotDeclared )
        string _name{};
    };
} // namespace sw::fixture
