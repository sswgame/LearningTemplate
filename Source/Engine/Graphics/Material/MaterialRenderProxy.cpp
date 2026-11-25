#include "pch.h"

#include "Engine/Graphics/Material/MaterialRenderProxy.h"

#include "Engine/Graphics/RHI/ConstantBufferPagePool.h"
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

    RHIBufferSRVIndex MaterialRenderProxy::sync( IRHIDevice* pDevice, const shared_ptr<const MaterialRenderData>& data )
    {
        if ( pDevice == nullptr || data == nullptr || data->_bytes.empty() )
            return kInvalidDescriptorIndex;
        pDevice->assumeRenderThreadRole(); // 이 객체는 렌더 스레드만 고친다
        // 백엔드가 바뀌었으면 구간은 옛 디바이스 것이다 — `write` 가 새 구간으로 바꾸며 옛 디바이스에 돌려준다(죽은 디바이스면 통보가 먼저 와서 잊게 했다).
        const bool bUpToDate = _constant.isValid() && _constant.getDevice() == pDevice && _uploaded == data;
        if ( bUpToDate )
            return _constant.getDescriptorIndex();

        const uint32 byteSize = static_cast<uint32>( data->_bytes.size() );
        // 사본마다 새 구간이다 — 한 프레임이 두 패킷(주 화면 · 프리뷰)으로 같은 머티리얼의 다른 사본을 그려도 먼저 기록한 드로우는 제 구간을 읽는다. 크기가 바뀌어도(다시 쿠킹) 같은 길이다.
        if ( pDevice->getConstantBufferPagePool().write( *pDevice, data->_bytes.data(), byteSize, _constant ) == false )
        {
            SW_LOG_ERROR_ID( "MaterialRenderProxy.CouldNotCreateMaterial", "Could not get a material constant buffer slice of %# bytes", byteSize );
            _uploaded.reset();
            return kInvalidDescriptorIndex;
        }
        _uploaded = data;
        return _constant.getDescriptorIndex();
    }

    void MaterialRenderProxy::releaseRHI( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _constant.getDevice() != pDevice )
            return;
        // 렌더 스레드가 병렬 기록 중이면 디바이스가 반환을 그 프레임 뒤로 미룬다(마지막 소유를 게임 스레드가 놓을 수 있다 — 풀이 놓은 줄에 넣는다).
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
