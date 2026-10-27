#include "pch.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    bool RHIStructuredBufferSlot::ensureCapacity( IRHIDevice* pDevice, uint32 elementSize, uint32 elementCount, RHIBufferUsage usage,
                                                  bool bNeedsSrv, bool bNeedsUav, const void* pInitialData )
    {
        if ( pDevice == nullptr || elementSize == 0 )
            return false;

        // 원소 크기가 달라지면 담을 수 있는 개수와 무관하게 다시 만들어야 한다. 구조버퍼의 stride 는
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

        // 한 번만 만든다 — UAV 를 뺀 버퍼나 다른 생성 함수로 대신 만들지 않는다. 컴퓨트(UAV)를 못 쓰는 백엔드는 부르는 쪽이 능력(`RHICapabilities::_bCompute`)으로 가린다.
        _buffer = pDevice->getResourceFactory()->createBuffer( desc );
        if ( _buffer == 0 )
        {
            // 프레임마다 다시 묻는 슬롯(GPUScene)이 있어 한 번만 남긴다 — 어느 슬롯인지는 부르는 쪽이 알린다.
            SW_LOG_ERROR_ONCE( "Failed to create a structured buffer (%# elements x %# bytes, usage %#) - the slot stays empty", elementCount, elementSize,
                               static_cast<uint32>( usage ) );
            return false;
        }

        if ( bNeedsSrv )
            _srv = pDevice->getResourceFactory()->registerBindlessBufferSrv( _buffer );
        if ( bNeedsUav )
            _uav = pDevice->getResourceFactory()->registerBindlessBufferUav( _buffer );
        _capacityElements = elementCount;
        _elementSize      = elementSize;
        return true;
    }

    void RHIStructuredBufferSlot::upload( IRHIDevice* pDevice, const void* pData, uint32 byteSize, uint32 byteOffset ) const
    {
        const bool bNothingToUpload = pDevice == nullptr || _buffer == 0 || pData == nullptr || byteSize == 0;
        if ( bNothingToUpload )
            return;
        pDevice->getResourceFactory()->updateStructuredBufferRange( _buffer, pData, byteSize, byteOffset );
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
            pDevice->getResourceFactory()->unregisterBindlessResource( _srv );
        if ( _uav != kInvalidDescriptorIndex )
            pDevice->getResourceFactory()->unregisterBindlessUav( _uav );
        pDevice->getResourceFactory()->destroyBuffer( _buffer );
        *this = RHIStructuredBufferSlot{};
    }
} // namespace sw
