/**
 * @file ReflectBuiltinsLoader.h
 * @brief ReflectBuiltins.h 스캔 → TypeNameMap/ContainerTypeMap + TypeRegistrar .gen.cpp emit.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) parse — ReflectBuiltins.h 스캔 → TypeNameMap / ContainerTypeMap
	// ------------------------------------------------------------------------------
	/** @brief 맵을 비운 뒤 builtins 헤더에서 등록을 채웁니다. */
	bool loadReflectBuiltins( const string_view absPath );

	// ------------------------------------------------------------------------------
	// 2) emit — primitive TypeRegistrar .gen.cpp
	// ------------------------------------------------------------------------------
	/**
	 * @brief builtins 헤더를 스캔해 primitive TypeRegistrar .gen.cpp 를 씁니다.
	 * @details EmitTemplateStore 의 Builtin*.tpl 사용 (--emit-templates 선행 로드 필요).
	 */
	bool emitReflectBuiltinsGen( const string_view builtinsAbsPath, const string_view outCppAbsPath );
} // namespace sw
