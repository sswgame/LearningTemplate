/**
 * @file D3D11RHIResourceFactoryBindless.cpp
 * @brief DirectX 11 의 bindless 등록입니다. 리소스를 셰이더가 인덱스로 접근할 수 있게 등록합니다.
 * @details `D3D11RHIResourceFactory` 의 일부입니다. DX12/Vulkan 은 디스크립터 힙과 배열을 쓰고, DX11/GL 은 슬롯
 *          기반이라 인덱스만 흉내 냅니다. 네 백엔드를 나란히 비교하기 좋은 곳입니다.
 */
#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResourceFactory.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    RHIDescriptorIndex D3D11RHIResourceFactory::registerBindlessTexture( RHITextureHandle texture )
    {
        _pDevice->assertRegistryMutableNow( "registerBindlessTexture" );
        if ( texture == 0 )
            return kInvalidDescriptorIndex;

        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_srv == nullptr )
            return kInvalidDescriptorIndex;
        // 끝나지 않은 업로드가 남은 텍스처는 등록하지 않는다(소비자는 pollTransfer 가 Complete 를 준 뒤에 등록한다 — IRHIResourceFactory 전송 계약 머리말).
        if ( _pDevice->_transferSerials.isTextureReadyToRegister( pRecord->_lastTransferSerial.load(), texture ) == false )
            return kInvalidDescriptorIndex;
        // 셰이더의 텍스처 슬롯은 Texture2D 로 선언돼 있다. 배열이나 큐브 뷰를 바인딩하면 셰이더가 읽지 못한다(RHITextureDimension 참고).
        if ( pRecord->_dimension != RHITextureDimension::Texture2D )
        {
            SW_LOG_ERROR( "registerBindlessTexture: texture %# is a %# texture — only Texture2D can be bound to shader texture slots yet", texture,
                          static_cast<uint32>( pRecord->_dimension ) );
            return kInvalidDescriptorIndex;
        }

        std::unique_lock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return allocateFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, texture );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerBindlessBufferSrv( RHIBufferHandle buffer )
    {
        _pDevice->assertRegistryMutableNow( "registerBindlessBufferSrv" );
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return kInvalidDescriptorIndex;
        std::unique_lock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return allocateFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree, buffer );
    }

    void D3D11RHIResourceFactory::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        _pDevice->assertRegistryMutableNow( "unregisterBindlessResource" );
        std::unique_lock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        // 이중 해제 가드는 `releaseFreeListIndex` 안에 있다.
        releaseFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree, index, RHIBufferHandle{ 0 } );
    }

    void D3D11RHIResourceFactory::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        _pDevice->assertRegistryMutableNow( "unregisterBindlessTexture" );
        std::unique_lock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, index, RHITextureHandle{ 0 } );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerBindlessBufferUav( RHIBufferHandle buffer )
    {
        _pDevice->assertRegistryMutableNow( "registerBindlessBufferUav" );
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;
        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return kInvalidDescriptorIndex;

        D3D11_BUFFER_DESC desc;
        pResource->GetDesc( &desc );

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
        if ( FAILED( _pDevice->_device->CreateUnorderedAccessView( pResource, &uavDesc, uav.GetAddressOf() ) ) )
            return kInvalidDescriptorIndex;

        return registerUavView( uav.Get(), buffer );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerBindlessTextureUav( RHITextureHandle texture )
    {
        _pDevice->assertRegistryMutableNow( "registerBindlessTextureUav" );
        if ( texture == 0 )
            return kInvalidDescriptorIndex;
        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr )
            return kInvalidDescriptorIndex;

        // 텍스처 UAV: bindComputeUav( index, kComputeTextureUav0 + 서수 ) 가 CSSetUnorderedAccessViews 로 바인딩한다.
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        if ( FAILED( _pDevice->_device->CreateUnorderedAccessView( pRecord->_texture.Get(), nullptr, uav.GetAddressOf() ) ) )
            return kInvalidDescriptorIndex;

        return registerUavView( uav.Get(), RHIBufferHandle{ 0 } );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerUavView( ID3D11UnorderedAccessView* pUav, RHIBufferHandle sourceBuffer )
    {
        std::unique_lock<shared_mutex> lock{ _pDevice->_bindlessMutex };
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

    void D3D11RHIResourceFactory::unregisterBindlessUav( RHIDescriptorIndex index )
    {
        _pDevice->assertRegistryMutableNow( "unregisterBindlessUav" );
        std::unique_lock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index >= _pDevice->_listRegisteredUAV.size() || _pDevice->_listRegisteredUAV[index] == nullptr )
            return;
        _pDevice->_listRegisteredUAV[index].Reset();
        _pDevice->_listUavSourceBuffer[index] = RHIBufferHandle{ 0 };
        _pDevice->_listUavFree.push_back( index );
    }
} // namespace sw
#endif
