#include "pch.h"

#include "Engine/Module/ModuleObjectRegistrar.h"

namespace sw
{
    ModuleObjectRegistrar*& ModuleObjectRegistrar::getHead()
    {
        static ModuleObjectRegistrar* s_pHead = nullptr;
        return s_pHead;
    }

    ModuleObjectRegistrar::ModuleObjectRegistrar()
        : _pNext{ getHead() }
    {
        getHead() = this;
    }
} // namespace sw
