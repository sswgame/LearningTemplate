#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    bool D3D12RHIDevice::createGlobalResources()
    {
        D3D12_DESCRIPTOR_RANGE descriptorRanges[11]{};
        // b0 = PassCB, b1 = MaterialCB. 둘은 힙에서 인접하지 않으므로 테이블을 나눈다.
        descriptorRanges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descriptorRanges[0].NumDescriptors                    = 1;
        descriptorRanges[0].BaseShaderRegister                = 0;
        descriptorRanges[0].RegisterSpace                     = 0;
        descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        for ( uint32 subpassIndex = 0; subpassIndex < 4; ++subpassIndex )
        {
            descriptorRanges[1 + subpassIndex].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            descriptorRanges[1 + subpassIndex].NumDescriptors                    = 1;
            descriptorRanges[1 + subpassIndex].BaseShaderRegister                = subpassIndex;
            descriptorRanges[1 + subpassIndex].RegisterSpace                     = 0;
            descriptorRanges[1 + subpassIndex].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }
        for ( uint32 subpassIndex = 0; subpassIndex < 4; ++subpassIndex )
        {
            descriptorRanges[5 + subpassIndex].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            descriptorRanges[5 + subpassIndex].NumDescriptors                    = 1;
            descriptorRanges[5 + subpassIndex].BaseShaderRegister                = subpassIndex;
            descriptorRanges[5 + subpassIndex].RegisterSpace                     = 0;
            descriptorRanges[5 + subpassIndex].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }
        descriptorRanges[9].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRanges[9].NumDescriptors                    = kBindlessTextureCount;
        descriptorRanges[9].BaseShaderRegister                = 0;
        descriptorRanges[9].RegisterSpace                     = 1;
        descriptorRanges[9].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        descriptorRanges[10].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descriptorRanges[10].NumDescriptors                    = 1;
        descriptorRanges[10].BaseShaderRegister                = 1;
        descriptorRanges[10].RegisterSpace                     = 0;
        descriptorRanges[10].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParameters[12]{};
        for ( uint32 paramIndex = 0; paramIndex < 10; ++paramIndex )
        {
            rootParameters[paramIndex].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParameters[paramIndex].DescriptorTable.NumDescriptorRanges = 1;
            rootParameters[paramIndex].DescriptorTable.pDescriptorRanges   = &descriptorRanges[paramIndex];
            rootParameters[paramIndex].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }
        rootParameters[kComputeRootConstantsParam].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[kComputeRootConstantsParam].Constants.ShaderRegister = 0;
        rootParameters[kComputeRootConstantsParam].Constants.RegisterSpace  = 1; ///< bindless.hlsli: b0, space1 (g_BindlessCbIndex)
        rootParameters[kComputeRootConstantsParam].Constants.Num32BitValues = kMaxComputeRootConstantDwords;
        rootParameters[kComputeRootConstantsParam].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[kMaterialCbvParam].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[kMaterialCbvParam].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[kMaterialCbvParam].DescriptorTable.pDescriptorRanges   = &descriptorRanges[10];
        rootParameters[kMaterialCbvParam].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC staticSamplers[2]{};
        staticSamplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSamplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSamplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSamplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSamplers[0].MipLODBias       = 0.0f;
        staticSamplers[0].MaxAnisotropy    = 1;
        staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
        staticSamplers[0].BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        staticSamplers[0].MinLOD           = 0.0f;
        staticSamplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
        staticSamplers[0].ShaderRegister   = 0;
        staticSamplers[0].RegisterSpace    = 0;
        staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        staticSamplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
        staticSamplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[1].MipLODBias       = 0.0f;
        staticSamplers[1].MaxAnisotropy    = 1;
        staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
        staticSamplers[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        staticSamplers[1].MinLOD           = 0.0f;
        staticSamplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
        staticSamplers[1].ShaderRegister   = 1;
        staticSamplers[1].RegisterSpace    = 0;
        staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters     = _countof( rootParameters );
        rootSigDesc.pParameters       = rootParameters;
        rootSigDesc.NumStaticSamplers = _countof( staticSamplers );
        rootSigDesc.pStaticSamplers   = staticSamplers;
        rootSigDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        _bHeapDirectlyIndexed = 0;

        auto tryCreateRootSigs = [&]( D3D12_ROOT_SIGNATURE_FLAGS flags ) -> bool
        {
            rootSigDesc.Flags = flags;
            signatureBlob.Reset();
            errorBlob.Reset();
            if ( FAILED( D3D12SerializeRootSignature( &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signatureBlob, &errorBlob ) ) )
            {
                if ( errorBlob )
                    SW_LOG_ERROR( "Root Signature Serialize Error: %s", static_cast<const utf8*>( errorBlob->GetBufferPointer() ) );
                return false;
            }
            _rootSignature.Reset();
            _computeRootSignature.Reset();
            if ( FAILED( _device->CreateRootSignature( 0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS( _rootSignature.GetAddressOf() ) ) ) )
                return false;
            if ( FAILED( _device->CreateRootSignature( 0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS( _computeRootSignature.GetAddressOf() ) ) ) )
            {
                _rootSignature.Reset();
                return false;
            }
            return true;
        };

        const D3D12_ROOT_SIGNATURE_FLAGS kIndexedFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
        const D3D12_ROOT_SIGNATURE_FLAGS kFallbackFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        if ( tryCreateRootSigs( kIndexedFlags ) )
            _bHeapDirectlyIndexed = 1;
        else if ( tryCreateRootSigs( kFallbackFlags ) )
        {
            // 정적 Caps의 native bindless는 후보; 런타임은 supportsNativeBindlessSampling()/getCapabilities().
            _bHeapDirectlyIndexed = 0;
            SW_LOG_TRACE( "Native heap indexing unavailable — bind-at-draw root signature (caps._bNativeBindless=0)" );
        }
        else
            return false;

        D3D12_INDIRECT_ARGUMENT_DESC drawArg{};
        drawArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC drawCmdSigDesc{};
        drawCmdSigDesc.ByteStride       = sizeof( D3D12_DRAW_ARGUMENTS );
        drawCmdSigDesc.NumArgumentDescs = 1;
        drawCmdSigDesc.pArgumentDescs   = &drawArg;
        _device->CreateCommandSignature( &drawCmdSigDesc, nullptr, IID_PPV_ARGS( _drawCommandSignature.GetAddressOf() ) );

        D3D12_INDIRECT_ARGUMENT_DESC drawIndexedArg{};
        drawIndexedArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC drawIndexedCmdSigDesc{};
        drawIndexedCmdSigDesc.ByteStride       = sizeof( D3D12_DRAW_INDEXED_ARGUMENTS );
        drawIndexedCmdSigDesc.NumArgumentDescs = 1;
        drawIndexedCmdSigDesc.pArgumentDescs   = &drawIndexedArg;
        _device->CreateCommandSignature( &drawIndexedCmdSigDesc, nullptr, IID_PPV_ARGS( _drawIndexedCommandSignature.GetAddressOf() ) );

        D3D12_INDIRECT_ARGUMENT_DESC dispatchArg{};
        dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC dispatchCmdSigDesc{};
        dispatchCmdSigDesc.ByteStride       = sizeof( D3D12_DISPATCH_ARGUMENTS );
        dispatchCmdSigDesc.NumArgumentDescs = 1;
        dispatchCmdSigDesc.pArgumentDescs   = &dispatchArg;
        _device->CreateCommandSignature( &dispatchCmdSigDesc, nullptr, IID_PPV_ARGS( _dispatchCommandSignature.GetAddressOf() ) );

        {
            const RHIVertex arrFullscreenVert[3] = {
                {{ -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
                { { 3.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
                { { -1.0f, 3.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            };

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC resDesc{};
            resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            resDesc.Width            = sizeof( arrFullscreenVert );
            resDesc.Height           = 1;
            resDesc.DepthOrArraySize = 1;
            resDesc.MipLevels        = 1;
            resDesc.Format           = DXGI_FORMAT_UNKNOWN;
            resDesc.SampleDesc.Count = 1;
            resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            if ( FAILED( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr, IID_PPV_ARGS( _vertexBuffer.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "Failed to create fullscreen vertex buffer." );
                return false;
            }

            void* pMapped{ nullptr };
            if ( FAILED( _vertexBuffer->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            {
                SW_LOG_ERROR( "Failed to map fullscreen vertex buffer." );
                _vertexBuffer.Reset();
                return false;
            }
            Memory::copy( pMapped, arrFullscreenVert, sizeof( arrFullscreenVert ) );
            _vertexBuffer->Unmap( 0, nullptr );
        }

        return true;
    }

} // namespace sw
#endif
