#include "pch.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

namespace sw
{
    SW_DEFINE_REGISTRATION_CHAIN( ModuleSubsystemRegistrar::getChain, ModuleSubsystemRegistrar )

    ModuleSubsystemRegistrar::ModuleSubsystemRegistrar( ModuleSubsystemScope targetScope )
        : _pNext{ getChain().pushFront( this ) }
        , _targetScope{ targetScope }
    {
    }
} // namespace sw
