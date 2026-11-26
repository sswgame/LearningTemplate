// EXPECT-ERROR: is neither a component nor a type with RuntimeState / SaveGame properties
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct StateDeclarationUnusedType
    {
        REFLECT_BODY();
        PROPERTY()
        int32              _value{ 0 };
        SW_TRANSIENT int32 _cache{ 0 };
    };
} // namespace sw::fixture
