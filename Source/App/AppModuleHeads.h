#pragma once
/**
 * @file AppModuleHeads.h
 * @brief App.exe registrar heads (isolated from Core / MODULE DLLs)
 */
#include "Core/Utility/ModuleRegistrarHeads.h"

SW_DECLARE_MODULE_REGISTRAR_HEAD( swAppGvmHead, ::sw::GlobalVariableRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swAppTypeHead, ::sw::TypeRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swAppEnumHead, ::sw::EnumRegistrar );

#undef SW_GVM_MODULE_HEAD
#define SW_GVM_MODULE_HEAD() (::swAppGvmHead())
#undef SW_TYPE_MODULE_HEAD
#define SW_TYPE_MODULE_HEAD() (::swAppTypeHead())
#undef SW_ENUM_MODULE_HEAD
#define SW_ENUM_MODULE_HEAD() (::swAppEnumHead())
