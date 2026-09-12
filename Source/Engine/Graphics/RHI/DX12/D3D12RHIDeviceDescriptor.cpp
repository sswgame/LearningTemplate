#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    void D3D12RHIDevice::bindBindlessRootState( ID3D12GraphicsCommandList* pList )
    {
        if ( pList == nullptr || _cbvHeap == nullptr )
            return;
        ID3D12DescriptorHeap* heaps[] = { _cbvHeap.Get() };
        pList->SetDescriptorHeaps( 1, heaps );
        if ( _rootSignature == nullptr )
            return;
        // 유일한 테이블(텍스처 배열)이 힙 시작을 가리킨다 — 리스트가 사는 동안 바뀌지 않는다. 같은 루트 시그니처를
        // 그래픽스/컴퓨트 두 바인드 포인트에 건다(루트 인자는 바인드 포인트별로 따로 산다). 버퍼는 이후 루트 디스크립터로 건다.
        const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = _cbvHeap->GetGPUDescriptorHandleForHeapStart();
        pList->SetGraphicsRootSignature( _rootSignature.Get() );
        pList->SetGraphicsRootDescriptorTable( kBindlessTextureTableParam, heapStart );
        pList->SetComputeRootSignature( _rootSignature.Get() );
        pList->SetComputeRootDescriptorTable( kBindlessTextureTableParam, heapStart );
    }

    void D3D12RHIDevice::refreshConstantBufferViews()
    {
        if ( _device == nullptr || _cbvHeap == nullptr )
            return;

        // 링 상수버퍼의 CBV 는 이번 프레임 슬롯을 가리켜야 한다. 슬롯은 프레임당 한 번 바뀌므로 여기서 한 번에 맞춘다.
        // (드로우마다 하던 일이다 — updateConstantBuffer 주석 참고.) 기록 시작 전 단일 스레드 구간이라 락이 필요 없다.
        const uint32 slot = _frameRing.currentIndex();
        for ( BindlessResourceRecord& rec : _listRegisteredBindless )
        {
            if ( rec._resource == nullptr || rec._buffer == 0 )
                continue;
            const auto sizeIt = _mapCbAlignedSize.find( rec._buffer );
            if ( sizeIt == _mapCbAlignedSize.end() )
                continue; // 링 상수버퍼가 아니다 (구조버퍼 SRV 등은 주소가 안 바뀐다).

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
            cbvDesc.BufferLocation = rec._resource->GetGPUVirtualAddress() + static_cast<UINT64>( slot ) * sizeIt->second;
            cbvDesc.SizeInBytes    = sizeIt->second;
            _device->CreateConstantBufferView( &cbvDesc, rec._cpuHandle );
        }
    }

    bool D3D12RHIDevice::createGlobalResources()
    {
        // 루트 시그니처 (bindingslots.hlsli) — 언리얼 FD3D12RootSignature 와 같은 배치: CB 는 루트 CBV, t/u 슬롯은 테이블, 텍스처는 배열 테이블.
        //  [0..2]  루트 CBV  b0..b2 (PassCB / MaterialCB·컴퓨트 CB / 예비)
        //  [3]     테이블: t0..t9 space0 (컴퓨트 읽기 t0..t3, 인스턴스 t4, 머티리얼 데이터 t9 — 오프라인 뷰를 드로우 직전 온라인 블록에 복사)
        //  [4]     테이블: u0..u3 space0 (컴퓨트 쓰기)
        //  [5]     테이블: t0 space1 무제한 텍스처 배열 + u0 space1 무제한 RW 텍스처 배열 (둘 다 힙 시작). SM6.6 ResourceDescriptorHeap 은 쓰지 않는다.
        //  [6]     32비트 루트 상수 b0 space2 (setComputeRootConstants)
        //  정적 샘플러 s0..s7 (bindingslots.hlsli 4 의 세트), space0.
        // 비용: shaderslot::dx12::kRootSignatureDwords = 3*2 + 3*1 + 16 = 25 dword (한계 64). 예전엔 t/u 도 루트 디스크립터라 51 이었다.
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            if ( SUCCEEDED( _device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) ) ) &&
                 options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_2 )
            {
                SW_LOG_ERROR( "Resource Binding Tier %# — 무제한 텍스처 배열에는 Tier 2 이상이 필요합니다.",
                              static_cast<uint32>( options.ResourceBindingTier ) );
            }
        }

        D3D12_ROOT_PARAMETER arrParam[kRootParameterCount]{};
        auto                 setRootDescriptor = [&]( uint32 paramIndex, D3D12_ROOT_PARAMETER_TYPE type, uint32 shaderRegister )
        {
            arrParam[paramIndex].ParameterType             = type;
            arrParam[paramIndex].Descriptor.ShaderRegister = shaderRegister;
            arrParam[paramIndex].Descriptor.RegisterSpace  = 0;
            arrParam[paramIndex].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        };
        for ( uint32 slot = 0; slot < shaderslot::kConstantBufferSlotCount; ++slot )
            setRootDescriptor( kCbvRootParam0 + slot, D3D12_ROOT_PARAMETER_TYPE_CBV, slot );

        // t/u 슬롯 테이블 — 범위 하나씩. 테이블 시작은 드로우/디스패치 직전 flushSlotTables 가 온라인 블록에 굳혀 건다.
        D3D12_DESCRIPTOR_RANGE srvSlotRange{};
        srvSlotRange.RangeType                                       = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvSlotRange.NumDescriptors                                  = shaderslot::kSrvSlotCount;
        srvSlotRange.BaseShaderRegister                              = 0;
        srvSlotRange.RegisterSpace                                   = 0;
        srvSlotRange.OffsetInDescriptorsFromTableStart               = 0;
        arrParam[kSrvTableParam].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        arrParam[kSrvTableParam].DescriptorTable.NumDescriptorRanges = 1;
        arrParam[kSrvTableParam].DescriptorTable.pDescriptorRanges   = &srvSlotRange;
        arrParam[kSrvTableParam].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE uavSlotRange{};
        uavSlotRange.RangeType                                       = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavSlotRange.NumDescriptors                                  = shaderslot::kComputeUavSlotCount;
        uavSlotRange.BaseShaderRegister                              = 0;
        uavSlotRange.RegisterSpace                                   = 0;
        uavSlotRange.OffsetInDescriptorsFromTableStart               = 0;
        arrParam[kUavTableParam].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        arrParam[kUavTableParam].DescriptorTable.NumDescriptorRanges = 1;
        arrParam[kUavTableParam].DescriptorTable.pDescriptorRanges   = &uavSlotRange;
        arrParam[kUavTableParam].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 텍스처 테이블 — 범위 둘이 같은 힙 시작을 가리킨다: t0 space1 = Texture2D g_SwBindlessTex2D[], u0 space1 = RWTexture2D
        // g_SwBindlessRWTex2D[] (컴퓨트). 인덱스는 등록이 준 힙 슬롯이라 둘 다 offset 0 이다.
        D3D12_DESCRIPTOR_RANGE arrTextureRange[2]{};
        arrTextureRange[0].RangeType                                             = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        arrTextureRange[0].NumDescriptors                                        = UINT_MAX; // 무제한
        arrTextureRange[0].BaseShaderRegister                                    = 0;
        arrTextureRange[0].RegisterSpace                                         = shaderslot::bindless::kTextureSpace;
        arrTextureRange[0].OffsetInDescriptorsFromTableStart                     = 0;
        arrTextureRange[1].RangeType                                             = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        arrTextureRange[1].NumDescriptors                                        = UINT_MAX;
        arrTextureRange[1].BaseShaderRegister                                    = 0;
        arrTextureRange[1].RegisterSpace                                         = shaderslot::bindless::kTextureSpace;
        arrTextureRange[1].OffsetInDescriptorsFromTableStart                     = 0;
        arrParam[kBindlessTextureTableParam].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        arrParam[kBindlessTextureTableParam].DescriptorTable.NumDescriptorRanges = 2;
        arrParam[kBindlessTextureTableParam].DescriptorTable.pDescriptorRanges   = arrTextureRange;
        arrParam[kBindlessTextureTableParam].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        arrParam[kRootConstantsParam].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        arrParam[kRootConstantsParam].Constants.ShaderRegister = shaderslot::kRootConstantRegister;
        arrParam[kRootConstantsParam].Constants.RegisterSpace  = shaderslot::kRootConstantSpace;
        arrParam[kRootConstantsParam].Constants.Num32BitValues = kMaxComputeRootConstantDwords;
        arrParam[kRootConstantsParam].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

        // 정적 샘플러 세트 s0..s7 (bindingslots.hlsli 4, 언리얼의 정적 샘플러와 같은 자리) — 셰이더는 g_SwSamplers[SW_SAMPLER_*].
        D3D12_STATIC_SAMPLER_DESC staticSamplers[shaderslot::kStaticSamplerCount]{};
        struct StaticSamplerSpec
        {
            D3D12_FILTER               _filter;
            D3D12_TEXTURE_ADDRESS_MODE _address;
            D3D12_COMPARISON_FUNC      _comparison;
            uint32                     _anisotropy;
        };
        const StaticSamplerSpec arrSpec[shaderslot::kStaticSamplerCount] = {
            {                 D3D12_FILTER_MIN_MAG_MIP_LINEAR,   D3D12_TEXTURE_ADDRESS_MODE_WRAP,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // LINEAR_WRAP
            {                 D3D12_FILTER_MIN_MAG_MIP_LINEAR,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // LINEAR_CLAMP
            {                  D3D12_FILTER_MIN_MAG_MIP_POINT,   D3D12_TEXTURE_ADDRESS_MODE_WRAP,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // POINT_WRAP
            {                  D3D12_FILTER_MIN_MAG_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // POINT_CLAMP
            {                 D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_MIRROR,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // LINEAR_MIRROR
            {                        D3D12_FILTER_ANISOTROPIC,   D3D12_TEXTURE_ADDRESS_MODE_WRAP,     D3D12_COMPARISON_FUNC_ALWAYS, 8}, // ANISO_WRAP
            {                  D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // POINT_BORDER
            {D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_COMPARISON_FUNC_LESS_EQUAL, 1}, // SHADOW_CMP
        };
        for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerCount; ++samplerIndex )
        {
            D3D12_STATIC_SAMPLER_DESC& sampler = staticSamplers[samplerIndex];
            sampler.Filter                     = arrSpec[samplerIndex]._filter;
            sampler.AddressU                   = arrSpec[samplerIndex]._address;
            sampler.AddressV                   = arrSpec[samplerIndex]._address;
            sampler.AddressW                   = arrSpec[samplerIndex]._address;
            sampler.MipLODBias                 = 0.0f;
            sampler.MaxAnisotropy              = arrSpec[samplerIndex]._anisotropy;
            sampler.ComparisonFunc             = arrSpec[samplerIndex]._comparison;
            sampler.BorderColor                = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            sampler.MinLOD                     = 0.0f;
            sampler.MaxLOD                     = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister             = samplerIndex;
            sampler.RegisterSpace              = 0;
            sampler.ShaderVisibility           = D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters     = kRootParameterCount;
        rootSigDesc.pParameters       = arrParam;
        rootSigDesc.NumStaticSamplers = _countof( staticSamplers );
        rootSigDesc.pStaticSamplers   = staticSamplers;
        rootSigDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        _bBindlessRootSignature = SW_FALSE;
        if ( FAILED( D3D12SerializeRootSignature( &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signatureBlob, &errorBlob ) ) )
        {
            if ( errorBlob )
                SW_LOG_ERROR( "Root Signature Serialize Error: %s", static_cast<const utf8*>( errorBlob->GetBufferPointer() ) );
            return false;
        }
        if ( FAILED( _device->CreateRootSignature( 0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS( _rootSignature.GetAddressOf() ) ) ) )
        {
            SW_LOG_ERROR( "CreateRootSignature failed." );
            return false;
        }
        _bBindlessRootSignature = SW_TRUE;

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
