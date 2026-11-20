#include "pch.h"

#include "Engine/Graphics/Material/MaterialRenderProxy.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    shared_ptr<MaterialRenderProxy> MaterialRenderProxy::create()
    {
        return sw::make_shared<MaterialRenderProxy>( CreateKey{} );
    }

    MaterialRenderProxy::MaterialRenderProxy( CreateKey )
        : _constant{}
        , _uploaded{}
    {
    }

    MaterialRenderProxy::~MaterialRenderProxy()
    {
        // 디바이스가 먼저 죽었으면 clearRHI 가 이미 잊게 했다 — 남아 있으면 `_constant` 의 소멸이 살아 있는 디바이스에 돌려준다.
    }

    RHIDescriptorIndex MaterialRenderProxy::sync( IRHIDevice* pDevice, const shared_ptr<const MaterialRenderData>& data )
    {
        if ( pDevice == nullptr || data == nullptr || data->_bytes.empty() )
            return kInvalidDescriptorIndex;
        pDevice->assumeRenderThreadRole(); // 이 객체는 렌더 스레드만 고친다
        // 백엔드가 바뀌었으면 구간은 옛 디바이스 것이다 — `fit` 이 옛 디바이스에 돌려준다(죽은 디바이스면 통보가 먼저 와서 잊게 했다).
        const bool bUpToDate = _constant.isValid() && _constant.getDevice() == pDevice && _uploaded == data;
        if ( bUpToDate )
            return _constant.getDescriptorIndex();

        const uint32 byteSize = static_cast<uint32>( data->_bytes.size() );
        // 셰이더를 다시 쿠킹하면 레이아웃이 커질 수 있다 — 구간보다 큰 update 는 이웃 구간을 덮는다. 등급이 바뀌면 풀이 구간을 옮긴다(예전 "커지면 다시 만든다" 자리).
        if ( pDevice->getConstantBufferPagePool().fit( *pDevice, byteSize, _constant ) == false )
        {
            SW_LOG_ERROR_ID( "MaterialRenderProxy.CouldNotCreateMaterial", "Could not get a material constant buffer slice of %# bytes", byteSize );
            _uploaded.reset();
            return kInvalidDescriptorIndex;
        }
        ConstantBufferPagePool::update( _constant, data->_bytes.data(), byteSize );
        _uploaded = data;
        return _constant.getDescriptorIndex();
    }

    void MaterialRenderProxy::releaseRHI( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _constant.getDevice() != pDevice )
            return;
        // 렌더 스레드가 병렬 기록 중이면 디바이스가 반환을 그 프레임 뒤로 미룬다(마지막 소유를 게임 스레드가 놓을 수 있다 — 풀이 `releaseHandle` 로 놓는다).
        _constant.reset();
        _uploaded.reset();
    }

    void MaterialRenderProxy::clearRHI( IRHIDevice* pDevice )
    {
        if ( _constant.getDevice() != pDevice )
            return;
        _constant.detach(); // 돌려줄 곳이 없다
        _uploaded.reset();
    }
} // namespace sw
