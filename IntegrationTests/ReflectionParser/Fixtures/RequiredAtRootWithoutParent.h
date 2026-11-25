// EXPECT-ERROR: has no PROPERTY( InheritParent ) field
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct NoParentLink
    {
        REFLECT_BODY();
        PROPERTY( RequiredAtRoot )
        string _digits{};
    };
} // namespace sw::fixture
