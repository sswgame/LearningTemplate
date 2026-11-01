#include "pch.h"

#include "Engine/Graphics/Material/MaterialRenderProxy.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    SW_LOG_CALLER( "MaterialRenderProxy" );

    shared_ptr<MaterialRenderProxy> MaterialRenderProxy::create()
    {
        return sw::make_shared<MaterialRenderProxy>( CreateKey{} );
    }

    MaterialRenderProxy::MaterialRenderProxy( CreateKey )
        : _constant{}
        , _constantByteSize{ 0 }
        , _descriptorIndex{ kInvalidDescriptorIndex }
        , _uploaded{}
    {
    }

    MaterialRenderProxy::~MaterialRenderProxy()
    {
        // 디바이스가 먼저 죽었으면 forgetRHI 가 이미 비웠다 — 남아 있으면 살아 있는 디바이스다.
        if ( IRHIDevice* pLiveDevice = _constant.getLiveDevice() )
            releaseRHI( pLiveDevice );
    }

    RHIDescriptorIndex MaterialRenderProxy::sync( IRHIDevice* pDevice, const shared_ptr<const MaterialRenderData>& data )
    {
        if ( pDevice == nullptr || data == nullptr || data->_bytes.empty() )
            return kInvalidDescriptorIndex;
        pDevice->assumeRenderThreadRole(); // 이 객체는 렌더 스레드만 고친다
        // 백엔드가 바뀌었으면 버퍼는 옛 디바이스 것이다(죽은 디바이스면 통보가 먼저 와서 비웠다).
        if ( _constant.isResident() && _constant._pDevice != pDevice )
            releaseRHI( _constant._pDevice );
        const bool bUpToDate = _constant.isResident() && _uploaded == data && _descriptorIndex != kInvalidDescriptorIndex;
        if ( bUpToDate )
            return _descriptorIndex;

        const uint32 byteSize = static_cast<uint32>( data->_bytes.size() );
        // 셰이더를 다시 쿠킹하면 레이아웃이 커질 수 있다 — 만들 때보다 큰 update 는 셋 백엔드가 검사 없이 버퍼 밖을 쓴다(GL 만 API 가 막는다). 커지면 다시 만든다.
        if ( _constant.isResident() && byteSize > _constantByteSize )
            releaseRHI( pDevice );
        if ( _constant.isResident() == false )
        {
            const RHIBufferHandle buffer = pDevice->getResourceFactory()->createConstantBuffer( byteSize );
            if ( buffer == 0 )
            {
                SW_LOG_ERROR( "Could not create a material constant buffer of %# bytes", byteSize );
                return kInvalidDescriptorIndex;
            }
            _constant.adopt( pDevice, buffer );
            _constantByteSize = byteSize;
            _descriptorIndex  = pDevice->getResourceFactory()->registerBindlessBufferSRV( buffer );
        }
        pDevice->getResourceFactory()->updateConstantBuffer( _constant._buffer, data->_bytes.data(), byteSize );
        _uploaded = data;
        return _descriptorIndex;
    }

    void MaterialRenderProxy::releaseRHI( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _constant._pDevice != pDevice )
            return;
        // 렌더 스레드가 병렬 기록 중이면 디바이스가 반환을 그 프레임 뒤로 미룬다(마지막 소유를 게임 스레드가 놓을 수 있다).
        if ( _descriptorIndex != kInvalidDescriptorIndex )
            pDevice->releaseHandle( RHIHandleKind::BindlessResource, _descriptorIndex );
        if ( _constant._buffer != 0 )
            pDevice->releaseHandle( RHIHandleKind::Buffer, _constant._buffer );
        forgetHandles();
    }

    void MaterialRenderProxy::forgetRHI( IRHIDevice* pDevice )
    {
        if ( _constant._pDevice == pDevice )
            forgetHandles();
    }

    void MaterialRenderProxy::forgetHandles()
    {
        _constant.forget();
        _constantByteSize = 0;
        _descriptorIndex  = kInvalidDescriptorIndex;
        _uploaded.reset();
    }
} // namespace sw
