// EXPECT-ERROR: a container has no single default value
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct BadDefault
    {
        REFLECT_BODY();
        PROPERTY( Default = "1" )
        vector<int32> _listValue{};
    };
} // namespace sw::fixture
