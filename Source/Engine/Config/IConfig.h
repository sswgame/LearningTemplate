#pragma once
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct SW_API IConfig
    {
        IConfig()                                = default;
        virtual ~IConfig()                       = default;
        IConfig( const IConfig& )                = default;
        IConfig& operator=( const IConfig& )     = default;
        IConfig( IConfig&& ) noexcept            = default;
        IConfig& operator=( IConfig&& ) noexcept = default;

        REFLECT_BODY();
    };
} // namespace sw
