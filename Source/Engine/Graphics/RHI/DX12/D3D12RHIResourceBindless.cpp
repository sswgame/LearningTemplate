/**
 * @file D3D12RHIResourceBindless.cpp
 * @brief DirectX 12 의 bindless 등록 — 리소스를 셰이더가 인덱스로 접근할 수 있게 올린다
 * @details `D3D12RHIResource` 의 일부다. DX12/Vulkan 은 디스크립터 힙/배열에 쓰고, DX11/GL 은 슬롯
 *          기반이라 인덱스만 흉내 낸다 — 네 백엔드를 나란히 비교하기 좋은 지점이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    namespace
    {
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "D3D12RHIResource" );

    RHIDescriptorIndex D3D12RHIResource::registerBindlessTexture( RHITextureHandle texture )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessTexture" );
        if ( texture == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        auto* pRes = _pDevice->resolveTexture( texture );
        if ( pRes == nullptr )
            return kInvalidDescriptorIndex;

        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  index;
        if ( _pDevice->_listFreeBindless.empty() == false )
        {
            index = _pDevice->_listFreeBindless.back();
            _pDevice->_listFreeBindless.pop_back();
        }
        else
        {
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kMaxShaderVisibleDescriptors )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kMaxShaderVisibleDescriptors );
                return kInvalidDescriptorIndex;
            }
            index = _pDevice->_allocatedDescriptorsCount++;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip     = 0;
        srvDesc.Texture2D.MipLevels           = pRes->GetDesc().MipLevels;
        srvDesc.Texture2D.PlaneSlice          = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        const DXGI_FORMAT resFmt = pRes->GetDesc().Format;
        if ( resFmt == DXGI_FORMAT_R24G8_TYPELESS || resFmt == DXGI_FORMAT_D24_UNORM_S8_UINT )
            srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        else
            srvDesc.Format = resFmt;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart() );
        cpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart() );
        gpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        _pDevice->_device->CreateShaderResourceView( pRes, &srvDesc, cpuHandle );

        if ( index >= _pDevice->_listRegisteredBindless.size() )
            _pDevice->_listRegisteredBindless.resize( index + 1 );
        _pDevice->_listRegisteredBindless[index]          = { pRes, cpuHandle, gpuHandle };
        _pDevice->_listRegisteredBindless[index]._texture = texture;

        return index;
    }

    RHIDescriptorIndex D3D12RHIResource::registerBindlessResource( RHIBufferHandle buffer )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessResource" );
        if ( buffer == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        auto* pRes = _pDevice->resolveBuffer( buffer );
        if ( pRes == nullptr )
            return kInvalidDescriptorIndex;

        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  index;
        if ( _pDevice->_listFreeBindless.empty() == false )
        {
            index = _pDevice->_listFreeBindless.back();
            _pDevice->_listFreeBindless.pop_back();
        }
        else
        {
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kMaxShaderVisibleDescriptors )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kMaxShaderVisibleDescriptors );
                return kInvalidDescriptorIndex;
            }
            index = _pDevice->_allocatedDescriptorsCount++;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart() );
        cpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart() );
        gpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        // 구조 버퍼면 StructuredBuffer SRV (ResourceDescriptorHeap[idx] 가 StructuredBuffer<T> 로 읽힘).
        // 그 외(상수 버퍼 ring)면 CBV.
        const auto strideIt = _pDevice->_mapStructuredStride.find( buffer );
        if ( strideIt != _pDevice->_mapStructuredStride.end() && strideIt->second > 0 )
        {
            const UINT                      stride = strideIt->second;
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.FirstElement        = 0;
            srvDesc.Buffer.NumElements         = static_cast<UINT>( pRes->GetDesc().Width ) / stride;
            srvDesc.Buffer.StructureByteStride = stride;
            srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
            _pDevice->_device->CreateShaderResourceView( pRes, &srvDesc, cpuHandle );

            if ( index >= _pDevice->_listRegisteredBindless.size() )
                _pDevice->_listRegisteredBindless.resize( index + 1 );
            _pDevice->_listRegisteredBindless[index]         = { pRes, cpuHandle, gpuHandle };
            _pDevice->_listRegisteredBindless[index]._buffer = buffer;
            return index;
        }

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
        cbvDesc.BufferLocation = pRes->GetGPUVirtualAddress();

        const auto sizeIt = _pDevice->_mapCbAlignedSize.find( buffer );
        if ( sizeIt != _pDevice->_mapCbAlignedSize.end() )
        {
            cbvDesc.BufferLocation += static_cast<UINT64>( _pDevice->_frameRing.currentIndex() ) * sizeIt->second;
            cbvDesc.SizeInBytes = sizeIt->second;
        }
        else
        {
            // Non-ring buffers: CBV size must be 256-byte aligned and <= resource width.
            const UINT width = static_cast<UINT>( pRes->GetDesc().Width );
            // 텍스처 행 정렬(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) — 상수버퍼 정렬과 이름만 같은 별개 값이다.
            const UINT aligned  = MathUtil::align( width, 256u );
            cbvDesc.SizeInBytes = ( aligned <= width ) ? aligned : ( width & ~255u );
            if ( cbvDesc.SizeInBytes == 0 )
                return kInvalidDescriptorIndex;
        }

        _pDevice->_device->CreateConstantBufferView( &cbvDesc, cpuHandle );

        if ( index >= _pDevice->_listRegisteredBindless.size() )
            _pDevice->_listRegisteredBindless.resize( index + 1 );
        _pDevice->_listRegisteredBindless[index]         = { pRes, cpuHandle, gpuHandle };
        _pDevice->_listRegisteredBindless[index]._buffer = buffer;

        return index;
    }

    void D3D12RHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        _pDevice->checkRegistryMutableNow( "unregisterBindlessResource" );
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index < _pDevice->_listRegisteredBindless.size() )
        {
            _pDevice->_listRegisteredBindless[index]._resource = nullptr;
            _pDevice->_listRegisteredBindless[index]._buffer   = 0;
            _pDevice->_listRegisteredBindless[index]._texture  = 0;
            _pDevice->_listFreeBindless.push_back( index );
        }
    }

    RHIDescriptorIndex D3D12RHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessUAV" );
        if ( buffer == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        ID3D12Resource* pRes = _pDevice->resolveBuffer( buffer );
        if ( pRes == nullptr )
            return kInvalidDescriptorIndex;

        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  descriptorIndex{ 0 };
        bool                                bReuseHeapSlot = false;
        if ( _pDevice->_listFreeUav.empty() == false )
        {
            descriptorIndex = _pDevice->_listFreeUav.back();
            _pDevice->_listFreeUav.pop_back();
            bReuseHeapSlot = true;
        }
        else
        {
            descriptorIndex = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() );
            _pDevice->_listRegisteredUAV.push_back( D3D12RHIDevice::BindlessResourceRecord{} );
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        if ( bReuseHeapSlot && descriptorIndex < _pDevice->_listRegisteredUAV.size() &&
             _pDevice->_listRegisteredUAV[descriptorIndex]._cpuHandle.ptr != 0 )
        {
            cpuHandle = _pDevice->_listRegisteredUAV[descriptorIndex]._cpuHandle;
            gpuHandle = _pDevice->_listRegisteredUAV[descriptorIndex]._gpuHandle;
        }
        else
        {
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kMaxShaderVisibleDescriptors )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kMaxShaderVisibleDescriptors );
                return kInvalidDescriptorIndex;
            }
            const RHIDescriptorIndex heapSlot = _pDevice->_allocatedDescriptorsCount++;
            cpuHandle                         = _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += heapSlot * _pDevice->_cbvDescriptorSize;
            gpuHandle = _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle.ptr += heapSlot * _pDevice->_cbvDescriptorSize;
        }

        if ( pRes->GetDesc().Width < 4 )
            return kInvalidDescriptorIndex;

        // RWByteAddressBuffer / RAW UAV: R32_TYPELESS + RAW, StructureByteStride must be 0.
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format                     = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.Buffer.FirstElement        = 0;
        uavDesc.Buffer.NumElements         = static_cast<UINT>( pRes->GetDesc().Width / 4 );
        uavDesc.Buffer.StructureByteStride = 0;
        uavDesc.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_RAW;

        _pDevice->_device->CreateUnorderedAccessView( pRes, nullptr, &uavDesc, cpuHandle );

        if ( descriptorIndex >= _pDevice->_listRegisteredUAV.size() )
            _pDevice->_listRegisteredUAV.resize( descriptorIndex + 1 );
        _pDevice->_listRegisteredUAV[descriptorIndex]         = { pRes, cpuHandle, gpuHandle };
        _pDevice->_listRegisteredUAV[descriptorIndex]._buffer = buffer;

        return descriptorIndex;
    }

    void D3D12RHIResource::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        _pDevice->checkRegistryMutableNow( "unregisterBindlessUAV" );
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index < _pDevice->_listRegisteredUAV.size() )
        {
            _pDevice->_listRegisteredUAV[index]._resource = nullptr;
            _pDevice->_listRegisteredUAV[index]._buffer   = 0;
            _pDevice->_listRegisteredUAV[index]._texture  = 0;
            _pDevice->_listFreeUav.push_back( index );
        }
    }

} // namespace sw
#endif
