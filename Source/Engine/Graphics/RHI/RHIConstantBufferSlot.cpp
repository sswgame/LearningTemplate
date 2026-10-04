#include "pch.h"

#include "Engine/Graphics/RHI/RHIConstantBufferSlot.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    bool RHIConstantBufferSlot::create( IRHIDevice* pDevice, uint32 byteSize )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr || byteSize == 0 )
            return false;
        if ( _buffer != 0 )
            return isValid();

        _buffer = pDevice->getResourceFactory()->createConstantBuffer( byteSize );
        if ( _buffer == 0 )
            return false;
        _index = pDevice->getResourceFactory()->registerBindlessResource( _buffer );
        return isValid();
    }

    void RHIConstantBufferSlot::update( IRHICommandList& cmd, const void* pData, uint32 byteSize ) const
    {
        if ( _buffer == 0 || pData == nullptr || byteSize == 0 )
            return;
        cmd.updateConstantBuffer( _buffer, pData, byteSize );
    }

    void RHIConstantBufferSlot::release( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
        {
            forget();
            return;
        }

        // **인덱스를 먼저 놓는다.** 버퍼를 먼저 지우면 레지스트리에 죽은 핸들을 가리키는 항목이 남는다.
        if ( _index != kInvalidDescriptorIndex )
            pDevice->getResourceFactory()->unregisterBindlessResource( _index );
        if ( _buffer != 0 )
            pDevice->getResourceFactory()->destroyBuffer( _buffer );
        forget();
    }
} // namespace sw
