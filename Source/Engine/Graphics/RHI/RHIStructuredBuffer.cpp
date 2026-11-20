#include "pch.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    bool RHIStructuredBuffer::ensureCapacity( IRHIDevice* pDevice, uint32 elementSize, uint32 elementCount, RHIBufferUsage usage,
                                              bool bNeedsSRV, bool bNeedsUAV, const void* pInitialData )
    {
        if ( pDevice == nullptr || elementSize == 0 )
            return false;

        // 원소 크기가 달라지면 담을 수 있는 개수와 무관하게 다시 만들어야 한다. 구조버퍼의 stride 는
        // 뷰에 박혀 있고, 셰이더 선언과 다르면 DX11 이 드로우마다 거부한다.
        const bool bEnough = ( _buffer != 0 ) && ( _elementSize == elementSize ) && ( _capacityElements >= elementCount );
        if ( bEnough )
            return true;

        // 같은 원소의 버퍼가 모자라 다시 만들 때는 1.5 배 이상으로 키운다 — 개수가 조금씩 늘 때(라이트 · 인스턴스) 프레임마다 다시 만들지 않는다.
        // 처음 만들 때와 원소 크기가 바뀔 때는 요청 그대로다(고정 크기 버퍼가 부풀지 않는다). 언리얼 GPUScene 버퍼의 늘림 정책과 같은 자리다(기억에 의존한 비교).
        const bool   bGrowing      = ( _buffer != 0 ) && ( _elementSize == elementSize );
        const uint64 grownCount    = static_cast<uint64>( _capacityElements ) + _capacityElements / 2u;
        const bool   bGrowthFits   = bGrowing && grownCount > elementCount && grownCount * elementSize <= std::numeric_limits<uint32>::max();
        const uint32 capacityCount = bGrowthFits ? static_cast<uint32>( grownCount ) : elementCount;

        release( pDevice );
        if ( elementCount == 0 )
            return false;

        // 내용은 요청한 원소 수만큼이다 — 용량이 더 크면 만든 뒤에 그만큼만 올린다(만들기가 용량 전체를 읽지 않게).
        const bool    bExactSize = capacityCount == elementCount;
        RHIBufferDesc desc{};
        desc._elementSize  = elementSize;
        desc._elementCount = capacityCount;
        desc._sizeBytes    = elementSize * capacityCount;
        desc._usage        = usage;
        desc._pInitialData = bExactSize ? pInitialData : nullptr;

        // 한 번만 만든다 — UAV 를 뺀 버퍼나 다른 생성 함수로 대신 만들지 않는다. 컴퓨트(UAV)를 못 쓰는 백엔드는 부르는 쪽이 능력(`RHICapabilities::_bCompute`)으로 가린다.
        _buffer = pDevice->getResourceFactory()->createBuffer( desc );
        if ( _buffer == 0 )
        {
            // 프레임마다 다시 묻는 슬롯(GPUScene)이 있어 한 번만 남긴다 — 어느 슬롯인지는 부르는 쪽이 알린다.
            SW_LOG_ERROR_ONCE_ID( "RHIStructuredBuffer.FailedCreateStructuredBuffer", "Failed to create a structured buffer (%# elements x %# bytes, usage %#) - the slot stays empty", elementCount, elementSize,
                                  static_cast<uint32>( usage ) );
            return false;
        }

        if ( bNeedsSRV )
            _srv = pDevice->getResourceFactory()->registerBindlessBufferSRV( _buffer );
        if ( bNeedsUAV )
            _uav = pDevice->getResourceFactory()->registerBindlessBufferUAV( _buffer );
        _capacityElements = capacityCount;
        _elementSize      = elementSize;
        if ( bExactSize == false )
            upload( pDevice, pInitialData, elementSize * elementCount );
        return true;
    }

    void RHIStructuredBuffer::upload( IRHIDevice* pDevice, const void* pData, uint32 byteSize, uint32 byteOffset ) const
    {
        const bool bNothingToUpload = pDevice == nullptr || _buffer == 0 || pData == nullptr || byteSize == 0;
        if ( bNothingToUpload )
            return;
        pDevice->getResourceFactory()->updateStructuredBufferRange( _buffer, pData, byteSize, byteOffset );
    }

    uint32 RHIStructuredBuffer::uploadRegions( IRHIDevice* pDevice, const void* pBaseSource, const vector<RHIBufferCopyRegion>& listRegion ) const
    {
        const bool bNothingToUpload = pDevice == nullptr || _buffer == 0 || pBaseSource == nullptr || listRegion.empty();
        if ( bNothingToUpload )
            return 0;
        pDevice->getResourceFactory()->updateStructuredBufferRegions( _buffer, pBaseSource, listRegion.data(), static_cast<uint32>( listRegion.size() ) );
        uint32 byteCount = 0;
        for ( const RHIBufferCopyRegion& region : listRegion )
        {
            byteCount += region._sizeBytes;
        }
        return byteCount;
    }

    void RHIStructuredBuffer::collectChangedRegions( const void* pPrevious, uint32 previousCount, const void* pCurrent, uint32 currentCount, uint32 stride,
                                                     uint32 mergeGapElements, vector<RHIBufferCopyRegion>& outListRegion )
    {
        outListRegion.clear();
        if ( pCurrent == nullptr || currentCount == 0 || stride == 0 )
            return;
        const uint8* const pPreviousByte = static_cast<const uint8*>( pPrevious );
        const uint8* const pCurrentByte  = static_cast<const uint8*>( pCurrent );
        const uint32       comparedCount = pPrevious != nullptr ? MathUtil::min( previousCount, currentCount ) : 0u;
        // 바뀐 구간 하나를 더한다. 앞 구간과의 사이가 붙일 만큼 짧으면 앞 구간을 늘린다.
        auto addRun = [&]( uint32 firstElement, uint32 endElement )
        {
            if ( outListRegion.empty() == false )
            {
                RHIBufferCopyRegion& last          = outListRegion.back();
                const uint32         lastEndOffset = last._dstOffset + last._sizeBytes;
                if ( firstElement * stride <= lastEndOffset + mergeGapElements * stride )
                {
                    last._sizeBytes = endElement * stride - last._dstOffset;
                    return;
                }
            }
            outListRegion.push_back( RHIBufferCopyRegion{ firstElement * stride, firstElement * stride, ( endElement - firstElement ) * stride } );
        };
        uint32 element = 0;
        while ( element < comparedCount )
        {
            if ( Memory::compare( pPreviousByte + element * stride, pCurrentByte + element * stride, stride ) == 0 )
            {
                ++element;
                continue;
            }
            const uint32 runBegin = element;
            while ( element < comparedCount && Memory::compare( pPreviousByte + element * stride, pCurrentByte + element * stride, stride ) != 0 )
            {
                ++element;
            }
            addRun( runBegin, element );
        }
        if ( currentCount > comparedCount )
            addRun( comparedCount, currentCount );
    }

    void RHIStructuredBuffer::release( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _buffer == 0 )
        {
            *this = RHIStructuredBuffer{};
            return;
        }

        // **뷰를 먼저 놓는다.** 버퍼를 먼저 지우면 레지스트리에 죽은 핸들을 가리키는 항목이 남는다.
        if ( _srv != kInvalidDescriptorIndex )
            pDevice->getResourceFactory()->unregisterBindlessResource( _srv );
        if ( _uav != kInvalidDescriptorIndex )
            pDevice->getResourceFactory()->unregisterBindlessUAV( _uav );
        pDevice->getResourceFactory()->destroyBuffer( _buffer );
        *this = RHIStructuredBuffer{};
    }
} // namespace sw
