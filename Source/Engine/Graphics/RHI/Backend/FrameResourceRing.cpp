#include "pch.h"

#include "Engine/Graphics/RHI/Backend/FrameResourceRing.h"

namespace sw
{
    FrameResourceRing::FrameResourceRing()
        : _arrFenceValue{}
        , _frameIndex{ constant::render::kMaxFrameCountInFlight - 1 }
    {
    }

    void FrameResourceRing::reset()
    {
        _frameIndex.store( constant::render::kMaxFrameCountInFlight - 1, std::memory_order_release );
        for ( uint64& fenceValue : _arrFenceValue )
        {
            fenceValue = 0;
        }
    }

    bool FrameResourceRing::beginFrame( uint64 completedFenceValue )
    {
        const uint32 nextIndex = ( _frameIndex.load( std::memory_order_relaxed ) + 1 ) % constant::render::kMaxFrameCountInFlight;
        if ( _arrFenceValue[nextIndex] > completedFenceValue )
            return false;

        _frameIndex.store( nextIndex, std::memory_order_release );
        return true;
    }

    uint64 FrameResourceRing::getFenceValue( uint32 index ) const
    {
        SW_CHECK_INDEX( index, constant::render::kMaxFrameCountInFlight ); // 링 칸 번호는 내부 값이다 — 개발 구성에서 알리고 배포는 아래 가드
        if ( index >= constant::render::kMaxFrameCountInFlight )
            return 0;
        return _arrFenceValue[index];
    }

    void FrameResourceRing::setFenceValue( uint32 index, uint64 fenceValue )
    {
        SW_CHECK_INDEX( index, constant::render::kMaxFrameCountInFlight );
        if ( index >= constant::render::kMaxFrameCountInFlight )
            return;
        _arrFenceValue[index] = fenceValue;
    }
} // namespace sw
