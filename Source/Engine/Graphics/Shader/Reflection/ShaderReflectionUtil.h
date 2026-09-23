/**
 * @file ShaderReflectionUtil.h
 * @brief ShaderReflection 포맷별 TU 가 함께 쓰는 선언입니다.
 */
#pragma once
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /** @brief 백엔드별 셰이더 리플렉션입니다. */
    struct ShaderReflectionUtil
    {
        static ShaderReflectionData reflectSpirv( const vector<uint8>& bytecode );

#if defined( SW_PLATFORM_WINDOWS )
        static ShaderReflectionData reflectDx( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat );
#endif
    };
} // namespace sw
