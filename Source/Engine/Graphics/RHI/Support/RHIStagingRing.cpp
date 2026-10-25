#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIStagingRing.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    RHIStagingRing::RHIStagingRing()
        : _mutex{}
        , _listEntry{}
        , _capacityBytes{ 0 }
        , _alignmentBytes{ 1 }
        , _nextID{ 1 }
    {
    }

    void RHIStagingRing::initialize( uint64 capacityBytes, uint64 alignmentBytes )
    {
        SW_ASSERT( MathUtil::isPowerOfTwo( alignmentBytes ) );
        std::scoped_lock<mutex> lock{ _mutex };
        _listEntry.clear();
        _capacityBytes  = capacityBytes;
        _alignmentBytes = alignmentBytes;
    }

    void RHIStagingRing::shutdown()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _listEntry.clear();
        _capacityBytes = 0;
    }

    RHIStagingAllocateResult RHIStagingRing::allocate( uint64 sizeBytes, uint64 completedSerial, RHIStagingSpan& outSpan )
    {
        outSpan = RHIStagingSpan{};
        std::scoped_lock<mutex> lock{ _mutex };
        if ( sizeBytes == 0 || sizeBytes > _capacityBytes )
            return RHIStagingAllocateResult::TooLarge;

        freeCompletedLocked( completedSerial );

        uint64 offsetBytes{ 0 };
        if ( _listEntry.empty() == false )
        {
            const Entry& front      = _listEntry.front();
            const Entry& back       = _listEntry.back();
            const uint64 backEnd    = MathUtil::align( back._offsetBytes + back._sizeBytes, _alignmentBytes );
            const bool   bWrapped   = back._offsetBytes < front._offsetBytes; // 산 구간이 링 끝을 넘어 처음으로 이어진다
            const bool   bFitsAtEnd = backEnd <= _capacityBytes && sizeBytes <= _capacityBytes - backEnd;
            if ( bWrapped )
            {
                // 산 구간 = [front, 끝) + [0, backEnd). 빈 곳은 [backEnd, front) 하나다.
                if ( backEnd > front._offsetBytes || sizeBytes > front._offsetBytes - backEnd )
                    return RHIStagingAllocateResult::Full;
                offsetBytes = backEnd;
            }
            else if ( bFitsAtEnd )
                offsetBytes = backEnd;
            else if ( sizeBytes <= front._offsetBytes )
                offsetBytes = 0; // 끝에 남은 자리는 버리고 처음으로 감는다 — 빈 곳은 [0, front) 다
            else
                return RHIStagingAllocateResult::Full;
        }

        const uint64 id = _nextID++;
        _listEntry.push_back( Entry{ id, offsetBytes, sizeBytes, 0, false } );
        outSpan = RHIStagingSpan{ offsetBytes, sizeBytes, id };
        return RHIStagingAllocateResult::Allocated;
    }

    void RHIStagingRing::markSubmitted( const RHIStagingSpan& span, uint64 serial )
    {
        SW_ASSERT( serial != 0 );
        std::scoped_lock<mutex> lock{ _mutex };
        Entry*                  pEntry = findEntryLocked( span._id );
        SW_ASSERT( pEntry != nullptr );
        if ( pEntry != nullptr )
            pEntry->_serial = serial;
    }

    void RHIStagingRing::free( const RHIStagingSpan& span )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        Entry*                  pEntry = findEntryLocked( span._id );
        SW_ASSERT( pEntry != nullptr );
        if ( pEntry != nullptr )
            pEntry->_bFreed = true;
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
        return static_cast<uint32>( _listEntry.size() );
    }

    void RHIStagingRing::freeCompletedLocked( uint64 completedSerial )
    {
        while ( _listEntry.empty() == false )
        {
            const Entry& front      = _listEntry.front();
            const bool   bCompleted = front._serial != 0 && front._serial <= completedSerial;
            if ( front._bFreed == false && bCompleted == false )
                break;
            _listEntry.pop_front();
        }
    }

    RHIStagingRing::Entry* RHIStagingRing::findEntryLocked( uint64 id )
    {
        // 산 구간은 몇 개뿐이고 방금 잡은 것이 뒤에 있다 — 뒤에서부터 찾는다.
        for ( auto entryIt = _listEntry.rbegin(); entryIt != _listEntry.rend(); ++entryIt )
        {
            if ( entryIt->_id == id )
                return &*entryIt;
        }
        return nullptr;
    }
} // namespace sw
