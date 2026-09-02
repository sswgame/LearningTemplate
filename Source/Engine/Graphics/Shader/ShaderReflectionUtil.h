/**
 * @file ShaderReflectionUtil.h
 * @brief ShaderReflection 포맷별 TU 공유 선언.
 */
#pragma once
#include "Engine/Graphics/Shader/ShaderReflection.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /** @brief 백엔드별 셰이더 리플렉션 */
    struct ShaderReflectionUtil
    {
        static ShaderReflectionData reflectSpirv( const vector<uint8>& bytecode );

#if defined( SW_PLATFORM_WINDOWS )
        static ShaderReflectionData reflectDx( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat );
#endif
    };
} // namespace sw
