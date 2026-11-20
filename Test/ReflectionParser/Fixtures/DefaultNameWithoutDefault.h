// EXPECT-ERROR: is not a name/path field with a C++ string default in the header
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct DefaultNameWithoutDefaultType
    {
        REFLECT_BODY();
        PROPERTY( DefaultName )
        string _layer{};
    };
} // namespace sw::fixture
