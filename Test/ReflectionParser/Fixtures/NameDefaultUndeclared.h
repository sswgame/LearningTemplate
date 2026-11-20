// EXPECT-ERROR: a name/path field with a C++ string default is used silently
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct NameDefaultUndeclaredType
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Bone the foot measure reads" )
        string _footBone{ "foot.l" };
    };
} // namespace sw::fixture
