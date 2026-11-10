#include "pch.h"

#include "Engine/Module/ModuleObjectRegistrar.h"

namespace sw
{
    SW_DEFINE_REGISTRATION_CHAIN( ModuleObjectRegistrar::getChain, ModuleObjectRegistrar )

    ModuleObjectRegistrar::ModuleObjectRegistrar()
        : _pNext{ getChain().pushFront( this ) }
    {
    }
} // namespace sw
