#pragma once
/**
 * @file ModuleRegistrarHeads.h
 * @brief Per-module GVM / Type / Enum registrar list heads (hot-reload safe)
 *
 * Core.dll keeps GlobalVariableRegistrar::getHead() / TypeRegistrar::getHead().
 * Other modules declare heads in a header and define them in exactly one .cpp
 * (avoids -Wunique-object-duplication from inline static locals in shared libs).
 */

#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Reflection/TypeRegistry.h"

/** @brief Declares a module-local registrar head getter (header). */
#define SW_DECLARE_MODULE_REGISTRAR_HEAD( FnName, RegistrarType ) RegistrarType*& FnName()

/** @brief Defines a module-local registrar head getter (exactly one .cpp). */
#define SW_DEFINE_MODULE_REGISTRAR_HEAD( FnName, RegistrarType ) \
	RegistrarType*& FnName()                                    \
	{                                                           \
		static RegistrarType* s_head = nullptr;                 \
		return s_head;                                          \
	}
