#include "pch.h"

#include "Engine/Graphics/RHI/RHIConstantBufferSlot.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    bool RHIConstantBufferSlot::create( IRHIDevice* pDevice, uint32 byteSize )
    {
        if ( pDevice == nullptr || pDevice->getResource() == nullptr || byteSize == 0 )
            return false;
        if ( _buffer != 0 )
            return isValid();

        _buffer = pDevice->getResource()->createConstantBuffer( byteSize );
        if ( _buffer == 0 )
            return false;
        _index = pDevice->getResource()->registerBindlessResource( _buffer );
        return isValid();
    }

    void RHIConstantBufferSlot::update( IRHIDevice* pDevice, const void* pData, uint32 byteSize ) const
    {
        if ( pDevice == nullptr || pDevice->getResource() == nullptr || _buffer == 0 || pData == nullptr || byteSize == 0 )
            return;
        pDevice->getResource()->updateConstantBuffer( _buffer, pData, byteSize );
    }

    void RHIConstantBufferSlot::release( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || pDevice->getResource() == nullptr )
        {
            forget();
            return;
        }

        // **인덱스를 먼저 놓는다.** 버퍼를 먼저 지우면 레지스트리에 죽은 핸들을 가리키는 항목이 남는다.
        if ( _index != kInvalidDescriptorIndex )
            pDevice->getResource()->unregisterBindlessResource( _index );
        if ( _buffer != 0 )
            pDevice->getResource()->destroyBuffer( _buffer );
        forget();
    }
} // namespace sw
