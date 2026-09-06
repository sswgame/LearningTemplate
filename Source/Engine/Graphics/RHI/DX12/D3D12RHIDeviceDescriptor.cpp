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
        // 레지스터 범위는 전부 계약(bindingslots.hlsli → shaderslot)에서 온다. 테이블 하나 = 루트 파라미터 하나.
        // 예전엔 SRV 테이블이 t0..t3 뿐이라 인스턴스 버퍼(t4)·머티리얼 텍스처(t5..t8)는 힙 직접 인덱싱이 없는
        // 기기에서 조용히 안 걸렸다 — 이제 SW_SRV_SLOT_COUNT 만큼 만든다.
        D3D12_DESCRIPTOR_RANGE descriptorRanges[kRootParameterCount]{};
        auto                   setRange = [&]( uint32 paramIndex, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 baseRegister, uint32 space, uint32 count )
        {
            descriptorRanges[paramIndex].RangeType                         = type;
            descriptorRanges[paramIndex].NumDescriptors                    = count;
            descriptorRanges[paramIndex].BaseShaderRegister                = baseRegister;
            descriptorRanges[paramIndex].RegisterSpace                     = space;
            descriptorRanges[paramIndex].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        };
        setRange( kPassCbvParam, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, shaderslot::kPassConstantBuffer, 0, 1 );
        for ( uint32 uavIndex = 0; uavIndex < shaderslot::kComputeUavSlotCount; ++uavIndex )
            setRange( kComputeUavRootParam0 + uavIndex, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavIndex, 0, 1 );
        for ( uint32 srvIndex = 0; srvIndex < shaderslot::kSrvSlotCount; ++srvIndex )
            setRange( kGraphicsSrvRootParam0 + srvIndex, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvIndex, 0, 1 );
        setRange( kBindlessTextureTableParam, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, shaderslot::kBindlessTextureSpace, kBindlessTextureCount );
        setRange( kMaterialCbvParam, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, shaderslot::kMaterialConstantBuffer, 0, 1 );

        D3D12_ROOT_PARAMETER rootParameters[kRootParameterCount]{};
        for ( uint32 paramIndex = 0; paramIndex < kRootParameterCount; ++paramIndex )
        {
            if ( paramIndex == kComputeRootConstantsParam )
                continue;
            rootParameters[paramIndex].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParameters[paramIndex].DescriptorTable.NumDescriptorRanges = 1;
            rootParameters[paramIndex].DescriptorTable.pDescriptorRanges   = &descriptorRanges[paramIndex];
            rootParameters[paramIndex].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }
        rootParameters[kComputeRootConstantsParam].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[kComputeRootConstantsParam].Constants.ShaderRegister = shaderslot::kBindlessConstantRegister;
        rootParameters[kComputeRootConstantsParam].Constants.RegisterSpace  = shaderslot::kBindlessConstantSpace; ///< b0, space1 (g_BindlessCbIndex)
        rootParameters[kComputeRootConstantsParam].Constants.Num32BitValues = kMaxComputeRootConstantDwords;
        rootParameters[kComputeRootConstantsParam].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

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
