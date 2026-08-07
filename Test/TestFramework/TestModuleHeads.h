#pragma once
/**
 * @file TestModuleHeads.h
 * @brief Unit-test executable registrar heads (defined in TestFramework.cpp)
 */
#include "Core/Utility/ModuleRegistrarHeads.h"

SW_DECLARE_MODULE_REGISTRAR_HEAD( swTestGvmHead, ::sw::GlobalVariableRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swTestTypeHead, ::sw::TypeRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swTestEnumHead, ::sw::EnumRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swTestComponentFactoryHead, ::sw::ComponentFactoryRegistrar );

#undef SW_GVM_MODULE_HEAD
#define SW_GVM_MODULE_HEAD() (::swTestGvmHead())
#undef SW_TYPE_MODULE_HEAD
#define SW_TYPE_MODULE_HEAD() (::swTestTypeHead())
#undef SW_ENUM_MODULE_HEAD
#define SW_ENUM_MODULE_HEAD() (::swTestEnumHead())
#undef SW_COMPONENT_FACTORY_MODULE_HEAD
#define SW_COMPONENT_FACTORY_MODULE_HEAD() (::swTestComponentFactoryHead())
