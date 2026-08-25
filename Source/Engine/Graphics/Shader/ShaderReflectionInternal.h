/**
 * @file ShaderReflectionInternal.h
 * @brief ShaderReflection 포맷별 TU 공유 선언.
 */
#pragma once
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"

namespace sw::shader_reflection_detail
{
	ShaderReflectionData reflectSpirv( const vector<uint8>& bytecode );

#if defined( SW_PLATFORM_WINDOWS )
	ShaderReflectionData reflectDx( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat );
#endif
} // namespace sw::shader_reflection_detail
