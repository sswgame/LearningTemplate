#pragma once
/**
 * @file EditorModuleHeads.h
 * @brief EditorModule.dll registrar heads
 */
#include "Core/Utility/ModuleRegistrarHeads.h"

SW_DECLARE_MODULE_REGISTRAR_HEAD( swEditorGvmHead, ::sw::GlobalVariableRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swEditorTypeHead, ::sw::TypeRegistrar );
SW_DECLARE_MODULE_REGISTRAR_HEAD( swEditorEnumHead, ::sw::EnumRegistrar );

#undef SW_GVM_MODULE_HEAD
#define SW_GVM_MODULE_HEAD() (::swEditorGvmHead())
#undef SW_TYPE_MODULE_HEAD
#define SW_TYPE_MODULE_HEAD() (::swEditorTypeHead())
#undef SW_ENUM_MODULE_HEAD
#define SW_ENUM_MODULE_HEAD() (::swEditorEnumHead())
