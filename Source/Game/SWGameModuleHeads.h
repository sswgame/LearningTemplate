#pragma once
/**
 * @file SWGameModuleHeads.h
 * @brief SWGame MODULE registrar heads (included by SWGameTypes.h for *.gen.cpp)
 */
#include "Core/Utility/ModuleRegistrarHeads.h"

SW_DECLARE_MODULE_REGISTRAR_HEAD( swGameGvmHead, ::sw::GlobalVariableRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swGameTypeHead, ::sw::TypeRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swGameEnumHead, ::sw::EnumRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swGameComponentFactoryHead, ::sw::ComponentFactoryRegistrar );

#undef SW_GVM_MODULE_HEAD
#define SW_GVM_MODULE_HEAD() (::swGameGvmHead())
#undef SW_TYPE_MODULE_HEAD
#define SW_TYPE_MODULE_HEAD() (::swGameTypeHead())
#undef SW_ENUM_MODULE_HEAD
#define SW_ENUM_MODULE_HEAD() (::swGameEnumHead())
#undef SW_COMPONENT_FACTORY_MODULE_HEAD
#define SW_COMPONENT_FACTORY_MODULE_HEAD() (::swGameComponentFactoryHead())
