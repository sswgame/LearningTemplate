// EXPECT-ERROR: has an unknown token 'NoSuchToken'
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct BadToken
    {
        REFLECT_BODY();
        PROPERTY( NoSuchToken )
        int32 _value{ 0 };
    };
} // namespace sw::fixture
