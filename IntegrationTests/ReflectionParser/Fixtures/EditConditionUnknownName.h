// EXPECT-ERROR: names a property 'sw::fixture::BadEditCondition' does not declare
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct BadEditCondition
    {
        REFLECT_BODY();
        PROPERTY()
        uint8 _bEnabled{ 0 };
        PROPERTY( EditCondition = "_bEnabeld" )
        float32 _speed{ 0.0f };
    };
} // namespace sw::fixture
