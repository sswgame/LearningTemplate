/**
 * @file D3D11RHIResourceBindless.cpp
 * @brief DirectX 11 의 bindless 등록입니다. 리소스를 셰이더가 인덱스로 접근할 수 있게 올립니다.
 * @details `D3D11RHIResource` 의 일부입니다. DX12/Vulkan 은 디스크립터 힙 · 배열에 쓰고, DX11/GL 은 슬롯
 *          기반이라 인덱스만 흉내 냅니다. 네 백엔드를 나란히 비교하기 좋은 지점입니다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

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
        _pDevice->assertRegistryMutableNow( "registerBindlessTexture" );
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
        _pDevice->assertRegistryMutableNow( "registerBindlessResource" );
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
        _pDevice->assertRegistryMutableNow( "unregisterBindlessResource" );
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        // 이중 해제 가드는 `releaseFreeListIndex` 안에 있다. 종류 이름만 넘겨 로그를 맞춘다.
        releaseFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree, index, RHIBufferHandle{ 0 },
                              "buffer" );
    }

    void D3D11RHIResource::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        _pDevice->assertRegistryMutableNow( "unregisterBindlessTexture" );
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, index, RHITextureHandle{ 0 },
                              "texture" );
    }

    RHIDescriptorIndex D3D11RHIResource::registerBindlessUav( RHIBufferHandle buffer )
    {
        _pDevice->assertRegistryMutableNow( "registerBindlessUav" );
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

        return registerUavView( uav.Get(), buffer );
    }

    RHIDescriptorIndex D3D11RHIResource::registerBindlessTextureUav( RHITextureHandle texture )
    {
        _pDevice->assertRegistryMutableNow( "registerBindlessTextureUav" );
        if ( texture == 0 )
            return kInvalidDescriptorIndex;
        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr )
            return kInvalidDescriptorIndex;

        // 텍스처 UAV: bindComputeUav( index, kComputeTextureUav0 + 서수 ) 가 CSSetUnorderedAccessViews 로 건다.
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        if ( FAILED( _pDevice->_device->CreateUnorderedAccessView( pRecord->_texture.Get(), nullptr, uav.GetAddressOf() ) ) )
            return kInvalidDescriptorIndex;

        return registerUavView( uav.Get(), RHIBufferHandle{ 0 } );
    }

    RHIDescriptorIndex D3D11RHIResource::registerUavView( ID3D11UnorderedAccessView* pUav, RHIBufferHandle sourceBuffer )
    {
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( _pDevice->_listUavFree.empty() == false )
        {
            const RHIDescriptorIndex index = _pDevice->_listUavFree.back();
            _pDevice->_listUavFree.pop_back();
            _pDevice->_listRegisteredUAV[index]   = pUav;
            _pDevice->_listUavSourceBuffer[index] = sourceBuffer;
            return index;
        }
        const RHIDescriptorIndex index = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() );
        _pDevice->_listRegisteredUAV.push_back( pUav );
        _pDevice->_listUavSourceBuffer.push_back( sourceBuffer );
        return index;
    }

    void D3D11RHIResource::unregisterBindlessUav( RHIDescriptorIndex index )
    {
        _pDevice->assertRegistryMutableNow( "unregisterBindlessUav" );
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index >= _pDevice->_listRegisteredUAV.size() || _pDevice->_listRegisteredUAV[index] == nullptr )
            return;
        _pDevice->_listRegisteredUAV[index].Reset();
        _pDevice->_listUavSourceBuffer[index] = RHIBufferHandle{ 0 };
        _pDevice->_listUavFree.push_back( index );
    }
} // namespace sw
#endif
