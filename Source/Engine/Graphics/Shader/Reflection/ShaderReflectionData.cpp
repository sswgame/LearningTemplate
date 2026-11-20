#include "pch.h"

#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/WebGPUShaderContainer.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionUtil.h"

namespace sw
{
    namespace
    {
        struct ShaderReflectionInternal
        {
            /** @brief WebGPU 바인드 그룹 0 의 번호를 레지스터 번호로 되돌립니다(`shaderslot::wgpu` 의 띠). 바인딩 검사 · 머티리얼 패커가 DX11 과 같은 레지스터 번호를 본다. */
            static uint32 toRegister( uint32 binding )
            {
                namespace wgpu          = shaderslot::wgpu;
                const uint32 arrShift[] = { wgpu::kUShift, wgpu::kSShift, wgpu::kTShift, wgpu::kBShift };
                for ( const uint32 shift : arrShift )
                {
                    if ( binding >= shift )
                        return binding - shift;
                }
                return binding;
            }

            /** @brief WebGPU 꾸러미 안의 SPIR-V 를 읽고 번호를 레지스터로 되돌립니다. 꾸러미가 아니면 오류입니다. */
            static expected<ShaderReflectionData> reflectWebGPU( const vector<uint8>& bytecode )
            {
                vector_reference<const uint8> spirv;
                if ( WebGPUShaderContainer::findSpirv( bytecode, spirv ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the WGSL_WebGPU bytecode is not a WebGPU shader container" } );
                expected<ShaderReflectionData> reflected = ShaderReflectionUtil::reflectSPIRV( vector<uint8>( spirv.begin(), spirv.end() ) );
                if ( reflected.has_value() == false )
                    return reflected;
                ShaderReflectionData& data = *reflected;
                for ( ShaderBufferInfo& buffer : data._listConstantBuffer )
                {
                    buffer._bindPoint = toRegister( buffer._bindPoint );
                }
                for ( ShaderReflectedBinding& resource : data._listResource )
                {
                    // SPIR-V 는 읽기 · 쓰기 구조버퍼를 모두 StorageBuffer 로 보고한다 — WebGPU 는 띠(t · u)가 따로라 번호로 정확히 가른다. 번호를 레지스터로 되돌리면
                    // 띠가 사라져 읽기 t4(인스턴스)와 쓰기 u4(메시지 링)가 같은 4 가 되므로, 되돌리기 전에 종류 이름을 정한다.
                    if ( resource._kind == ShaderBindingKind::StructuredBuffer )
                        resource._kind = resource._bindPoint >= shaderslot::wgpu::kUShift ? ShaderBindingKind::RwStructuredBuffer : ShaderBindingKind::StructuredBuffer;
                    resource._bindPoint = toRegister( resource._bindPoint );
                }
                return reflected;
            }
        };
    } // namespace

    expected<ShaderReflectionData> ShaderReflectionData::reflect( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat )
    {
        if ( bytecode.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflect called with empty bytecode" } );

        switch ( targetFormat )
        {
            case ShaderTargetFormat::DXBC_D3D11:
            case ShaderTargetFormat::DXIL_D3D12:
#if defined( SW_PLATFORM_WINDOWS )
                return ShaderReflectionUtil::reflectDx( bytecode, targetFormat );
#elif defined( SW_PLATFORM_POSIX )
                return unexpected( Error{ ErrorKind::Unsupported, "DX bytecode reflection needs Windows (D3DReflect · DXC reflection)" } );
#else
    #error "Unsupported platform"
#endif
            case ShaderTargetFormat::SPIRV_Vulkan:
            case ShaderTargetFormat::SPIRV_OpenGL:
                return ShaderReflectionUtil::reflectSPIRV( bytecode );
            case ShaderTargetFormat::WGSL_WebGPU:
                return ShaderReflectionInternal::reflectWebGPU( bytecode );
            case ShaderTargetFormat::Count:
                break;
        }
        return unexpected( Error{ ErrorKind::InvalidArgument, "reflect called with an unknown shader target format" } );
    }
} // namespace sw
