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
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kBindlessDescriptorCapacity )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kBindlessDescriptorCapacity );
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
        // 같은 뷰를 오프라인 힙에도 만든다 — 슬롯 테이블(t#/u#)은 여기서 온라인 블록으로 복사한다.
        const D3D12_CPU_DESCRIPTOR_HANDLE offlineHandle = _pDevice->offlineDescriptorAt( index );

        _pDevice->_device->CreateShaderResourceView( pRes, &srvDesc, cpuHandle );
        _pDevice->_device->CreateShaderResourceView( pRes, &srvDesc, offlineHandle );

        if ( index >= _pDevice->_listRegisteredBindless.size() )
            _pDevice->_listRegisteredBindless.resize( index + 1 );
        _pDevice->_listRegisteredBindless[index]          = { pRes, cpuHandle, gpuHandle, offlineHandle };
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
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kBindlessDescriptorCapacity )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kBindlessDescriptorCapacity );
                return kInvalidDescriptorIndex;
            }
            index = _pDevice->_allocatedDescriptorsCount++;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart() );
        cpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart() );
        gpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;
        // 같은 뷰를 오프라인 힙에도 만든다 — 슬롯 테이블(t#/u#)은 여기서 온라인 블록으로 복사한다.
        const D3D12_CPU_DESCRIPTOR_HANDLE offlineHandle = _pDevice->offlineDescriptorAt( index );

        // 구조 버퍼면 StructuredBuffer SRV (셰이더의 StructuredBuffer<T> name[] 이 이 힙 인덱스로 읽는다).
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
            _pDevice->_device->CreateShaderResourceView( pRes, &srvDesc, offlineHandle );
            _pDevice->_device->CreateShaderResourceView( pRes, &srvDesc, offlineHandle );

            if ( index >= _pDevice->_listRegisteredBindless.size() )
                _pDevice->_listRegisteredBindless.resize( index + 1 );
            _pDevice->_listRegisteredBindless[index]         = { pRes, cpuHandle, gpuHandle, offlineHandle };
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
        _pDevice->_device->CreateConstantBufferView( &cbvDesc, offlineHandle );

        if ( index >= _pDevice->_listRegisteredBindless.size() )
            _pDevice->_listRegisteredBindless.resize( index + 1 );
        _pDevice->_listRegisteredBindless[index]         = { pRes, cpuHandle, gpuHandle, offlineHandle };
        _pDevice->_listRegisteredBindless[index]._buffer = buffer;

        return index;
    }

    void D3D12RHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        _pDevice->checkRegistryMutableNow( "unregisterBindlessResource" );
        releaseBindlessSlot( index );
    }

    void D3D12RHIResource::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        // DX12 는 텍스처와 버퍼가 같은 셰이더 가시 힙을 나눠 쓰므로 인덱스 공간이 하나다.
        _pDevice->checkRegistryMutableNow( "unregisterBindlessTexture" );
        releaseBindlessSlot( index );
    }

    void D3D12RHIResource::releaseBindlessSlot( RHIDescriptorIndex index )
    {
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index >= _pDevice->_listRegisteredBindless.size() )
            return;
        D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredBindless[index];
        // 이미 빈 슬롯을 다시 프리리스트에 넣으면 같은 인덱스가 두 리소스에 발급된다.
        if ( rec._resource == nullptr && rec._buffer == 0 && rec._texture == 0 )
        {
            SW_LOG_ERROR( "Bindless index %# is already free; ignoring the duplicate release.", index );
            return;
        }
        rec._resource = nullptr;
        rec._buffer   = 0;
        rec._texture  = 0;
        deferFreeBindlessIndex( index );
    }

    void D3D12RHIResource::deferFreeBindlessIndex( RHIDescriptorIndex index )
    {
        // 인덱스는 GPU 가 이 프레임까지의 커맨드를 다 읽은 뒤에야 재사용한다 (언리얼의 지연 디스크립터 해제와 같다).
        // 즉시 프리리스트에 넣으면 같은 프레임에 등록된 새 리소스가 그 자리를 받아, 아직 실행 중인 리스트가 새 리소스를 읽는다.
        D3D12RHIDevice* pDevice = _pDevice;
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pDevice, index]()
        {
            std::unique_lock<std::shared_mutex> lock{ pDevice->_bindlessMutex };
            pDevice->_listFreeBindless.push_back( index );
        } ),
                                                   _pDevice->_fenceValue );
    }

    RHIDescriptorIndex D3D12RHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessUAV" );
        if ( buffer == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        ID3D12Resource* pRes = _pDevice->resolveBuffer( buffer );
        if ( pRes == nullptr || pRes->GetDesc().Width < 4 )
            return kInvalidDescriptorIndex;

        // UAV 도 SRV/CBV 와 **같은 힙 인덱스 공간** 을 쓴다. 셰이더가 RWStructuredBuffer<T> name[] 을 이 인덱스로
        // 고르고, 루트 시그니처의 UAV 무제한 범위가 힙 시작(offset 0)을 가리키므로 인덱스 = 힙 슬롯이어야 한다.
        // 예전엔 UAV 목록의 순번을 돌려줘서 힙 슬롯과 달랐다(테이블을 슬롯마다 따로 걸던 시절엔 상관없었다).
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  index;
        if ( _pDevice->_listFreeBindless.empty() == false )
        {
            index = _pDevice->_listFreeBindless.back();
            _pDevice->_listFreeBindless.pop_back();
        }
        else
        {
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kBindlessDescriptorCapacity )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kBindlessDescriptorCapacity );
                return kInvalidDescriptorIndex;
            }
            index = _pDevice->_allocatedDescriptorsCount++;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart() );
        cpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart() );
        gpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;
        // 같은 뷰를 오프라인 힙에도 만든다 — 슬롯 테이블(t#/u#)은 여기서 온라인 블록으로 복사한다.
        const D3D12_CPU_DESCRIPTOR_HANDLE offlineHandle = _pDevice->offlineDescriptorAt( index );

        // 구조 버퍼면 StructuredBuffer UAV, 아니면 RAW UAV (RWByteAddressBuffer: R32_TYPELESS + RAW, stride 0).
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        const auto strideIt         = _pDevice->_mapStructuredStride.find( buffer );
        if ( strideIt != _pDevice->_mapStructuredStride.end() && strideIt->second > 0 )
        {
            uavDesc.Format                     = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.NumElements         = static_cast<UINT>( pRes->GetDesc().Width ) / strideIt->second;
            uavDesc.Buffer.StructureByteStride = strideIt->second;
            uavDesc.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_NONE;
        }
        else
        {
            uavDesc.Format                     = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Buffer.NumElements         = static_cast<UINT>( pRes->GetDesc().Width / 4 );
            uavDesc.Buffer.StructureByteStride = 0;
            uavDesc.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_RAW;
        }
        _pDevice->_device->CreateUnorderedAccessView( pRes, nullptr, &uavDesc, cpuHandle );
        _pDevice->_device->CreateUnorderedAccessView( pRes, nullptr, &uavDesc, offlineHandle );

        if ( index >= _pDevice->_listRegisteredUAV.size() )
            _pDevice->_listRegisteredUAV.resize( index + 1 );
        _pDevice->_listRegisteredUAV[index]         = { pRes, cpuHandle, gpuHandle, offlineHandle };
        _pDevice->_listRegisteredUAV[index]._buffer = buffer;

        return index;
    }

    RHIDescriptorIndex D3D12RHIResource::registerBindlessTextureUAV( RHITextureHandle texture )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessTextureUAV" );
        if ( texture == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;
        ID3D12Resource* pRes = _pDevice->resolveTexture( texture );
        if ( pRes == nullptr || ( pRes->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) == 0 )
            return kInvalidDescriptorIndex;

        // 텍스처 UAV 도 같은 힙 인덱스 공간 — 셰이더가 RWTexture2D g_SwBindlessRWTex2D[] (u0 space1) 을 이 인덱스로 고른다.
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  index;
        if ( _pDevice->_listFreeBindless.empty() == false )
        {
            index = _pDevice->_listFreeBindless.back();
            _pDevice->_listFreeBindless.pop_back();
        }
        else
        {
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kBindlessDescriptorCapacity )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kBindlessDescriptorCapacity );
                return kInvalidDescriptorIndex;
            }
            index = _pDevice->_allocatedDescriptorsCount++;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart() );
        cpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart() );
        gpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;
        // 같은 뷰를 오프라인 힙에도 만든다 — 슬롯 테이블(t#/u#)은 여기서 온라인 블록으로 복사한다.
        const D3D12_CPU_DESCRIPTOR_HANDLE offlineHandle = _pDevice->offlineDescriptorAt( index );

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format               = pRes->GetDesc().Format;
        uavDesc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice   = 0;
        uavDesc.Texture2D.PlaneSlice = 0;
        _pDevice->_device->CreateUnorderedAccessView( pRes, nullptr, &uavDesc, cpuHandle );
        _pDevice->_device->CreateUnorderedAccessView( pRes, nullptr, &uavDesc, offlineHandle );

        if ( index >= _pDevice->_listRegisteredUAV.size() )
            _pDevice->_listRegisteredUAV.resize( index + 1 );
        _pDevice->_listRegisteredUAV[index]          = { pRes, cpuHandle, gpuHandle, offlineHandle };
        _pDevice->_listRegisteredUAV[index]._texture = texture;
        return index;
    }

    void D3D12RHIResource::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        _pDevice->checkRegistryMutableNow( "unregisterBindlessUAV" );
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index >= _pDevice->_listRegisteredUAV.size() )
            return;
        D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredUAV[index];
        if ( rec._resource == nullptr && rec._buffer == 0 && rec._texture == 0 )
        {
            SW_LOG_ERROR( "Bindless UAV index %# is already free; ignoring the duplicate release.", index );
            return;
        }
        rec._resource = nullptr;
        rec._buffer   = 0;
        rec._texture  = 0;
        deferFreeBindlessIndex( index ); // 힙 인덱스 공간이 하나라 프리리스트도 하나다
    }

} // namespace sw
#endif
