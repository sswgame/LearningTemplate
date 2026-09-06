/**
 * @file D3D11RHIResourceBindless.cpp
 * @brief DirectX 11 의 bindless 등록 — 리소스를 셰이더가 인덱스로 접근할 수 있게 올린다
 * @details `D3D11RHIResource` 의 일부다. DX12/Vulkan 은 디스크립터 힙/배열에 쓰고, DX11/GL 은 슬롯
 *          기반이라 인덱스만 흉내 낸다 — 네 백엔드를 나란히 비교하기 좋은 지점이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
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
    SW_LOG_CALLER( "D3D11" );

    RHIDescriptorIndex D3D11RHIResource::registerBindlessTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return kInvalidDescriptorIndex;

        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_srv == nullptr )
            return kInvalidDescriptorIndex;

        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        return allocateFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, texture );
    }

    RHIDescriptorIndex D3D11RHIResource::registerBindlessResource( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        ID3D11Buffer* pRes = _pDevice->resolveBuffer( buffer );
        if ( pRes == nullptr )
            return kInvalidDescriptorIndex;
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        return allocateFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree, buffer );
    }

    void D3D11RHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        // 빈 슬롯(텍스처 인덱스가 잘못 넘어왔거나 이중 해제)을 다시 넣으면 같은 인덱스가 두 버퍼에 발급된다.
        if ( index < _pDevice->_listRegisteredBindless.size() && _pDevice->_listRegisteredBindless[index] == 0 )
        {
            SW_LOG_ERROR( "Bindless buffer index %# is already free; ignoring the duplicate release.", index );
            return;
        }
        releaseFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree, index, RHIBufferHandle{ 0 } );
    }

    void D3D11RHIResource::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index < _pDevice->_listRegisteredTexture.size() && _pDevice->_listRegisteredTexture[index] == 0 )
        {
            SW_LOG_ERROR( "Bindless texture index %# is already free; ignoring the duplicate release.", index );
            return;
        }
        releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, index, RHITextureHandle{ 0 } );
    }

    RHIDescriptorIndex D3D11RHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;
        ID3D11Buffer* pRes = _pDevice->resolveBuffer( buffer );
        if ( pRes == nullptr )
            return kInvalidDescriptorIndex;

        D3D11_BUFFER_DESC desc;
        pRes->GetDesc( &desc );

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        if ( ( desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS ) != 0 )
        {
            uavDesc.Format             = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Buffer.NumElements = desc.ByteWidth / 4;
            uavDesc.Buffer.Flags       = D3D11_BUFFER_UAV_FLAG_RAW;
        }
        else
        {
            if ( desc.StructureByteStride == 0 )
                return kInvalidDescriptorIndex;
            uavDesc.Format             = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.NumElements = desc.ByteWidth / desc.StructureByteStride;
        }

        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        if ( FAILED( _pDevice->_device->CreateUnorderedAccessView( pRes, &uavDesc, uav.GetAddressOf() ) ) )
            return kInvalidDescriptorIndex;

        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  index;
        if ( _pDevice->_listUavFree.empty() == false )
        {
            index = _pDevice->_listUavFree.back();
            _pDevice->_listUavFree.pop_back();
            _pDevice->_listRegisteredUAV[index]   = uav;
            _pDevice->_listUavSourceBuffer[index] = buffer;
        }
        else
        {
            index = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() );
            _pDevice->_listRegisteredUAV.push_back( uav );
            _pDevice->_listUavSourceBuffer.push_back( buffer );
        }
        return index;
    }

    void D3D11RHIResource::unregisterBindlessUAV( RHIDescriptorIndex index ) { (void)index; }
} // namespace sw
#endif
