#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIStagingAllocator.h"

#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    RHIStagingAllocator::RHIStagingAllocator()
        : _mutex{}
        , _slots{}
        , _capacityBytes{ 0 }
        , _alignmentBytes{ 1 }
        , _soloMaxBytes{ 0 }
        , _soloSizeBytes{ 0 }
        , _soloSerial{ 0 }
        , _soloReservationMicroseconds{ 0 }
        , _soloReservedUntilMicroseconds{ 0 }
        , _liveCount{ 0 }
    {
    }

    void RHIStagingAllocator::initialize( uint64 capacityBytes, uint64 alignmentBytes, uint64 soloMaxBytes, int64 soloReservationMicroseconds )
    {
        SW_ASSERT( MathUtil::isPowerOfTwo( alignmentBytes ) );
        SW_ASSERT( soloReservationMicroseconds >= 0 );
        const uint64 slotCount = capacityBytes / alignmentBytes;
        SW_ASSERT( slotCount < SpanAllocator::kInvalidOffset );
        ScopedLock<mutex> lock{ _mutex };
        _slots.reset( static_cast<uint32>( slotCount ) );
        _capacityBytes                 = slotCount * alignmentBytes;
        _alignmentBytes                = alignmentBytes;
        _soloMaxBytes                  = soloMaxBytes;
        _soloSizeBytes                 = 0;
        _soloSerial                    = 0;
        _soloReservationMicroseconds   = soloReservationMicroseconds;
        _soloReservedUntilMicroseconds = 0;
        _liveCount                     = 0;
    }

    void RHIStagingAllocator::shutdown()
    {
        ScopedLock<mutex> lock{ _mutex };
        _slots.reset( 0 );
        _capacityBytes                 = 0;
        _soloMaxBytes                  = 0;
        _soloSizeBytes                 = 0;
        _soloSerial                    = 0;
        _soloReservedUntilMicroseconds = 0;
        _liveCount                     = 0;
    }

    RHIStagingAllocateResult RHIStagingAllocator::allocate( uint64 sizeBytes, uint64 completedSerial, RHIStagingSpan& outSpan )
    {
        outSpan = RHIStagingSpan{};
        ScopedLock<mutex> lock{ _mutex };
        const bool        bSolo     = sizeBytes > _capacityBytes;
        const bool        bTooLarge = sizeBytes == 0 || ( bSolo && sizeBytes > _soloMaxBytes );
        if ( bTooLarge )
            return RHIStagingAllocateResult::TooLarge;

        freeCompletedLocked( completedSerial );
        if ( _soloSizeBytes != 0 )
            return RHIStagingAllocateResult::Full; // 단독 구간이 살아 있는 동안은 아무것도 받지 않는다 — 메모리 최고점이 max(링, 한 장)이다

        if ( bSolo )
        {
            if ( _liveCount != 0 )
            {
                // 링이 비기를 기다린다. 예약이 링 요청을 막아 링이 비게 하고, 다시 올 때마다 연장된다.
                if ( _soloReservationMicroseconds > 0 )
                    _soloReservedUntilMicroseconds = MonotonicClock::nowMicroseconds() + _soloReservationMicroseconds;
                return RHIStagingAllocateResult::Full;
            }
            _soloSizeBytes                 = sizeBytes;
            _soloSerial                    = 0;
            _soloReservedUntilMicroseconds = 0;
            ++_liveCount;
            outSpan = RHIStagingSpan{ 0, sizeBytes, SW_TRUE };
            return RHIStagingAllocateResult::Allocated;
        }

        if ( _soloReservedUntilMicroseconds != 0 )
        {
            if ( MonotonicClock::nowMicroseconds() < _soloReservedUntilMicroseconds )
                return RHIStagingAllocateResult::Full;
            _soloReservedUntilMicroseconds = 0; // 단독 요청이 다시 오지 않았다(포기) — 링 요청을 다시 받는다
        }
        const uint32 slot = _slots.allocate( toSlotCount( sizeBytes ) );
        if ( slot == SpanAllocator::kInvalidOffset )
            return RHIStagingAllocateResult::Full;

        ++_liveCount;
        outSpan = RHIStagingSpan{ static_cast<uint64>( slot ) * _alignmentBytes, sizeBytes, SW_FALSE };
        return RHIStagingAllocateResult::Allocated;
    }

    void RHIStagingAllocator::markSubmitted( const RHIStagingSpan& span, uint64 serial )
    {
        SW_ASSERT( serial != 0 );
        SW_ASSERT( span._sizeBytes != 0 );
        ScopedLock<mutex> lock{ _mutex };
        if ( span._bSolo != SW_FALSE )
        {
            SW_ASSERT( _soloSizeBytes == span._sizeBytes && _soloSerial == 0 );
            _soloSerial = serial;
            return;
        }
        _slots.freeDeferred( static_cast<uint32>( span._offsetBytes / _alignmentBytes ), toSlotCount( span._sizeBytes ), serial );
    }

    void RHIStagingAllocator::free( const RHIStagingSpan& span )
    {
        SW_ASSERT( span._sizeBytes != 0 );
        ScopedLock<mutex> lock{ _mutex };
        if ( span._bSolo != SW_FALSE )
        {
            SW_ASSERT( _soloSizeBytes == span._sizeBytes && _soloSerial == 0 );
            if ( _soloSizeBytes != 0 )
                releaseSoloLocked();
            return;
        }
        const uint32 allocatedBefore = _slots.getAllocatedCount();
        _slots.free( static_cast<uint32>( span._offsetBytes / _alignmentBytes ), toSlotCount( span._sizeBytes ) );
        // SpanAllocator 가 이중 반납 · 범위 밖을 단언하고 무시한다 — 그때는 산 구간 수도 그대로 둔다.
        if ( _slots.getAllocatedCount() != allocatedBefore )
            --_liveCount;
    }

    void RHIStagingAllocator::freeCompleted( uint64 completedSerial )
    {
        ScopedLock<mutex> lock{ _mutex };
        freeCompletedLocked( completedSerial );
    }

    uint64 RHIStagingAllocator::getCapacityBytes() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _capacityBytes;
    }

    uint64 RHIStagingAllocator::getSoloMaxBytes() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _soloMaxBytes;
    }

    uint32 RHIStagingAllocator::getLiveCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _liveCount;
    }

    bool RHIStagingAllocator::hasLiveSolo() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _soloSizeBytes != 0;
    }

    uint32 RHIStagingAllocator::toSlotCount( uint64 sizeBytes ) const
    {
        return static_cast<uint32>( MathUtil::align( sizeBytes, _alignmentBytes ) / _alignmentBytes );
    }

    void RHIStagingAllocator::freeCompletedLocked( uint64 completedSerial )
    {
        const uint32 deferredBefore = _slots.getDeferredSpanCount();
        _slots.retireDeferred( completedSerial );
        _liveCount -= deferredBefore - _slots.getDeferredSpanCount();
        const bool bSoloCompleted = _soloSerial != 0 && _soloSerial <= completedSerial;
        if ( bSoloCompleted )
            releaseSoloLocked();
    }

    void RHIStagingAllocator::releaseSoloLocked()
    {
        _soloSizeBytes = 0;
        _soloSerial    = 0;
        --_liveCount;
    }
} // namespace sw
