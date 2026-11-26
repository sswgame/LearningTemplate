// EXPECT-ERROR: carries more than one of PROPERTY(), SW_CODEC_STATE and SW_TRANSIENT
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct StateDeclarationConflictType
    {
        REFLECT_BODY();
        PROPERTY( RuntimeState )
        SW_TRANSIENT int32 _count{ 0 };
    };
} // namespace sw::fixture
