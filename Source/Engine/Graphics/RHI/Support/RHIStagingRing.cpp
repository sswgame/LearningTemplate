#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIStagingRing.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    RHIStagingRing::RHIStagingRing()
        : _mutex{}
        , _slots{}
        , _capacityBytes{ 0 }
        , _alignmentBytes{ 1 }
        , _liveCount{ 0 }
    {
    }

    void RHIStagingRing::initialize( uint64 capacityBytes, uint64 alignmentBytes )
    {
        SW_ASSERT( MathUtil::isPowerOfTwo( alignmentBytes ) );
        const uint64 slotCount = capacityBytes / alignmentBytes;
        SW_ASSERT( slotCount < SpanAllocator::kInvalidOffset );
        std::scoped_lock<mutex> lock{ _mutex };
        _slots.reset( static_cast<uint32>( slotCount ) );
        _capacityBytes  = slotCount * alignmentBytes;
        _alignmentBytes = alignmentBytes;
        _liveCount      = 0;
    }

    void RHIStagingRing::shutdown()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _slots.reset( 0 );
        _capacityBytes = 0;
        _liveCount     = 0;
    }

    RHIStagingAllocateResult RHIStagingRing::allocate( uint64 sizeBytes, uint64 completedSerial, RHIStagingSpan& outSpan )
    {
        outSpan = RHIStagingSpan{};
        std::scoped_lock<mutex> lock{ _mutex };
        if ( sizeBytes == 0 || sizeBytes > _capacityBytes )
            return RHIStagingAllocateResult::TooLarge;

        freeCompletedLocked( completedSerial );
        const uint32 slot = _slots.allocate( toSlotCount( sizeBytes ) );
        if ( slot == SpanAllocator::kInvalidOffset )
            return RHIStagingAllocateResult::Full;

        ++_liveCount;
        outSpan = RHIStagingSpan{ static_cast<uint64>( slot ) * _alignmentBytes, sizeBytes };
        return RHIStagingAllocateResult::Allocated;
    }

    void RHIStagingRing::markSubmitted( const RHIStagingSpan& span, uint64 serial )
    {
        SW_ASSERT( serial != 0 );
        SW_ASSERT( span._sizeBytes != 0 );
        std::scoped_lock<mutex> lock{ _mutex };
        _slots.freeDeferred( static_cast<uint32>( span._offsetBytes / _alignmentBytes ), toSlotCount( span._sizeBytes ), serial );
    }

    void RHIStagingRing::free( const RHIStagingSpan& span )
    {
        SW_ASSERT( span._sizeBytes != 0 );
        std::scoped_lock<mutex> lock{ _mutex };
        const uint32            allocatedBefore = _slots.getAllocatedCount();
        _slots.free( static_cast<uint32>( span._offsetBytes / _alignmentBytes ), toSlotCount( span._sizeBytes ) );
        // SpanAllocator 가 이중 반납 · 범위 밖을 단언하고 무시한다 — 그때는 산 구간 수도 그대로 둔다.
        if ( _slots.getAllocatedCount() != allocatedBefore )
            --_liveCount;
    }

    void RHIStagingRing::freeCompleted( uint64 completedSerial )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        freeCompletedLocked( completedSerial );
    }

    uint64 RHIStagingRing::getCapacityBytes() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _capacityBytes;
    }

    uint32 RHIStagingRing::getLiveCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _liveCount;
    }

    uint32 RHIStagingRing::toSlotCount( uint64 sizeBytes ) const
    {
        return static_cast<uint32>( MathUtil::align( sizeBytes, _alignmentBytes ) / _alignmentBytes );
    }

    void RHIStagingRing::freeCompletedLocked( uint64 completedSerial )
    {
        const uint32 deferredBefore = _slots.getDeferredSpanCount();
        _slots.retireDeferred( completedSerial );
        _liveCount -= deferredBefore - _slots.getDeferredSpanCount();
    }
} // namespace sw
