#include "pch.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"

#include "Core/Common/EnumUtil.h"

namespace sw
{
    bool RHIStructuredBufferSlot::ensureCapacity( IRHIDevice* pDevice, uint32 elementSize, uint32 elementCount, RHIBufferUsage usage,
                                                  bool bNeedsSrv, bool bNeedsUav, const void* pInitialData )
    {
        if ( pDevice == nullptr || elementSize == 0 )
            return false;

        // 원소 크기가 달라지면 담을 수 있는 개수와 무관하게 다시 만들어야 한다 — 구조버퍼의 stride 는
        // 뷰에 박혀 있고, 셰이더 선언과 다르면 DX11 이 드로우마다 거부한다.
        const bool bEnough = ( _buffer != 0 ) && ( _elementSize == elementSize ) && ( _capacityElements >= elementCount );
        if ( bEnough )
            return true;

        release( pDevice );
        if ( elementCount == 0 )
            return false;

        RHIBufferDesc desc{};
        desc._elementSize  = elementSize;
        desc._elementCount = elementCount;
        desc._sizeBytes    = elementSize * elementCount;
        desc._usage        = usage;
        desc._pInitialData = pInitialData;

        _buffer = pDevice->getResource()->createBuffer( desc );
        if ( _buffer == 0 && bNeedsUav )
        {
            // UAV 를 거절하는 백엔드·드라이버가 있다. 그리기는 살리고 컴퓨트 경로만 포기한다.
            desc._usage = EnumUtil::clearFlag( usage, RHIBufferUsage::UnorderedAccess );
            _buffer     = pDevice->getResource()->createBuffer( desc );
            bNeedsUav   = false;
        }
        if ( _buffer == 0 )
        {
            _buffer = pDevice->getResource()->createStructuredBuffer( elementSize, elementCount );
            if ( _buffer != 0 && pInitialData != nullptr )
                pDevice->getResource()->updateStructuredBuffer( _buffer, pInitialData, desc._sizeBytes );
        }
        if ( _buffer == 0 )
            return false;

        if ( bNeedsSrv )
            _srv = pDevice->getResource()->registerBindlessResource( _buffer );
        if ( bNeedsUav )
            _uav = pDevice->getResource()->registerBindlessUAV( _buffer );
        _capacityElements = elementCount;
        _elementSize      = elementSize;
        return true;
    }

    void RHIStructuredBufferSlot::upload( IRHIDevice* pDevice, const void* pData, uint32 byteSize ) const
    {
        if ( pDevice == nullptr || _buffer == 0 || pData == nullptr || byteSize == 0 )
            return;
        pDevice->getResource()->updateStructuredBuffer( _buffer, pData, byteSize );
    }

    void RHIStructuredBufferSlot::release( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _buffer == 0 )
        {
            *this = RHIStructuredBufferSlot{};
            return;
        }

        // **뷰를 먼저 놓는다.** 버퍼를 먼저 지우면 레지스트리에 죽은 핸들을 가리키는 항목이 남는다.
        if ( _srv != kInvalidDescriptorIndex )
            pDevice->getResource()->unregisterBindlessResource( _srv );
        if ( _uav != kInvalidDescriptorIndex )
            pDevice->getResource()->unregisterBindlessUAV( _uav );
        pDevice->getResource()->destroyBuffer( _buffer );
        *this = RHIStructuredBufferSlot{};
    }
} // namespace sw
