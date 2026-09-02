#include "pch.h"

#include "Engine/Graphics/Shader/ShaderReflection.h"

#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflectionUtil.h"

SW_LOG_CALLER( "ShaderReflection" );
namespace sw
{
    ShaderReflectionData ShaderReflection::reflect( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat )
    {
        if ( bytecode.empty() )
        {
            SW_LOG_WARNING( "Reflect called with empty bytecode." );
            return {};
        }

        switch ( targetFormat )
        {
            case ShaderTargetFormat::DXBC_D3D11:
            case ShaderTargetFormat::DXIL_D3D12:
#if defined( SW_PLATFORM_WINDOWS )
                return ShaderReflectionUtil::reflectDx( bytecode, targetFormat );
#else
                return {};
#endif
            case ShaderTargetFormat::SPIRV_Vulkan:
            case ShaderTargetFormat::SPIRV_OpenGL:
                return ShaderReflectionUtil::reflectSpirv( bytecode );
            case ShaderTargetFormat::Count:
            default:
                break;
        }
        return {};
    }
} // namespace sw
