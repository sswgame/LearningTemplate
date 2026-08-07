#pragma once
/**
 * @file AppInternal.h
 * @brief Shared App translation-unit helpers (module name constants)
 */
#include "Core/Common/Types.h"

namespace sw::app_internal
{
	inline constexpr const utf8* kEditorModuleName = "EditorModule";
#if !defined( SW_SHIPPING )
	inline constexpr const utf8* kGameModuleName = "SWGame";
#endif
} // namespace sw::app_internal
