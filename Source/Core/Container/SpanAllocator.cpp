#include "pch.h"

#include "Core/Container/SpanAllocator.h"

namespace sw
{
    SpanAllocator::SpanAllocator()
        : _listFreeSpan{}
        , _listDeferredSpan{}
        , _capacity{ 0 }
        , _allocatedCount{ 0 }
    {
    }

    SpanAllocator::SpanAllocator( uint32 capacity )
        : _listFreeSpan{}
        , _listDeferredSpan{}
        , _capacity{ 0 }
        , _allocatedCount{ 0 }
    {
        reset( capacity );
    }

    void SpanAllocator::reset( uint32 capacity )
    {
        _listFreeSpan.clear();
        _listDeferredSpan.clear();
        _capacity       = capacity;
        _allocatedCount = 0;
        if ( capacity > 0 )
            _listFreeSpan.push_back( Span{ 0, capacity } );
    }

    void SpanAllocator::grow( uint32 newCapacity )
    {
        if ( newCapacity <= _capacity )
            return;
        const uint32 oldCapacity = _capacity;
        _capacity                = newCapacity;
        // 꼬리에 빈 구간을 더한다 — 마지막 빈 구간이 옛 끝에 닿아 있으면 늘리고, 아니면 새로 둔다.
        const bool bTailFree = _listFreeSpan.empty() == false && _listFreeSpan.back()._offset + _listFreeSpan.back()._count == oldCapacity;
        if ( bTailFree )
            _listFreeSpan.back()._count += newCapacity - oldCapacity;
        else
            _listFreeSpan.push_back( Span{ oldCapacity, newCapacity - oldCapacity } );
    }

    uint32 SpanAllocator::allocate( uint32 count )
    {
        if ( count == 0 )
            return kInvalidOffset;

        // 가장 잘 맞는 빈 구간 — 크기가 같으면 곧바로, 아니면 가장 작게 남는 것. 큰 구간을 아껴 큰 메시가 들어갈 자리를 남긴다.
        size_t bestIndex = _listFreeSpan.size();
        for ( size_t index = 0; index < _listFreeSpan.size(); ++index )
        {
            const uint32 spanCount = _listFreeSpan[index]._count;
            if ( spanCount < count )
                continue;
            const bool bBetter = bestIndex == _listFreeSpan.size() || spanCount < _listFreeSpan[bestIndex]._count;
            if ( bBetter )
                bestIndex = index;
            if ( spanCount == count )
                break;
        }
        if ( bestIndex == _listFreeSpan.size() )
            return kInvalidOffset;

        Span&        span   = _listFreeSpan[bestIndex];
        const uint32 offset = span._offset;
        if ( span._count == count )
        {
            _listFreeSpan.erase( _listFreeSpan.begin() + static_cast<ptrdiff_t>( bestIndex ) );
        }
        else
        {
            span._offset += count;
            span._count -= count;
        }
        _allocatedCount += count;
        return offset;
    }

    void SpanAllocator::free( uint32 offset, uint32 count )
    {
        if ( count == 0 )
            return;
        if ( insertFreeSpan( offset, count ) )
            _allocatedCount -= count;
    }

    void SpanAllocator::freeDeferred( uint32 offset, uint32 count, uint64 stamp )
    {
        if ( count == 0 )
            return;
        const bool bInRange = offset < _capacity && count <= _capacity - offset;
        if ( bInRange == false )
        {
            SW_ASSERT( false && "SpanAllocator::freeDeferred: the span is outside the capacity; ignoring it" );
            return;
        }
        _listDeferredSpan.push_back( DeferredSpan{
            Span{ offset, count },
            stamp
        } );
    }

    void SpanAllocator::retireDeferred( uint64 stamp )
    {
        // 순서를 지키며 앞으로 당긴다 — 남는 것은 그대로 대기열에 둔다.
        size_t keepCount = 0;
        for ( size_t index = 0; index < _listDeferredSpan.size(); ++index )
        {
            const DeferredSpan deferred = _listDeferredSpan[index];
            if ( deferred._stamp <= stamp )
            {
                free( deferred._span._offset, deferred._span._count );
                continue;
            }
            _listDeferredSpan[keepCount] = deferred;
            ++keepCount;
        }
        _listDeferredSpan.resize( keepCount );
    }

    uint32 SpanAllocator::getLargestFreeCount() const
    {
        uint32 largest = 0;
        for ( const Span& span : _listFreeSpan )
        {
            if ( span._count > largest )
                largest = span._count;
        }
        return largest;
    }

    uint32 SpanAllocator::getUsedEnd() const
    {
        if ( _allocatedCount == 0 )
            return 0;
        // 마지막 빈 구간이 용량 끝에 닿아 있으면 그 시작이 끝이고, 아니면 용량 끝까지 쓰고 있다.
        const bool bTailFree = _listFreeSpan.empty() == false && _listFreeSpan.back()._offset + _listFreeSpan.back()._count == _capacity;
        return bTailFree ? _listFreeSpan.back()._offset : _capacity;
    }

    bool SpanAllocator::insertFreeSpan( uint32 offset, uint32 count )
    {
        const bool bInRange = offset < _capacity && count <= _capacity - offset;
        if ( bInRange == false )
        {
            SW_ASSERT( false && "SpanAllocator::free: the span is outside the capacity; ignoring it" );
            return false;
        }

        // 시작 오름차순 자리 — 첫 번째로 offset 보다 뒤에서 시작하는 빈 구간 앞이다.
        size_t insertIndex = 0;
        while ( insertIndex < _listFreeSpan.size() && _listFreeSpan[insertIndex]._offset < offset )
        {
            ++insertIndex;
        }

        // 앞 · 뒤 빈 구간과 겹치면 이미 빈 자리를 다시 돌려받는 것이다(이중 반납). 받으면 다음 두 할당이 같은 자리를 서로 다른 대상에 준다.
        const bool bOverlapsPrevious = insertIndex > 0 && _listFreeSpan[insertIndex - 1]._offset + _listFreeSpan[insertIndex - 1]._count > offset;
        const bool bOverlapsNext     = insertIndex < _listFreeSpan.size() && offset + count > _listFreeSpan[insertIndex]._offset;
        if ( bOverlapsPrevious || bOverlapsNext )
        {
            SW_ASSERT( false && "SpanAllocator::free: the span is already free; ignoring the duplicate release" );
            return false;
        }

        const bool bTouchesPrevious = insertIndex > 0 && _listFreeSpan[insertIndex - 1]._offset + _listFreeSpan[insertIndex - 1]._count == offset;
        const bool bTouchesNext     = insertIndex < _listFreeSpan.size() && offset + count == _listFreeSpan[insertIndex]._offset;
        if ( bTouchesPrevious && bTouchesNext )
        {
            _listFreeSpan[insertIndex - 1]._count += count + _listFreeSpan[insertIndex]._count;
            _listFreeSpan.erase( _listFreeSpan.begin() + static_cast<ptrdiff_t>( insertIndex ) );
        }
        else if ( bTouchesPrevious )
        {
            _listFreeSpan[insertIndex - 1]._count += count;
        }
        else if ( bTouchesNext )
        {
            _listFreeSpan[insertIndex]._offset = offset;
            _listFreeSpan[insertIndex]._count += count;
        }
        else
        {
            _listFreeSpan.insert( _listFreeSpan.begin() + static_cast<ptrdiff_t>( insertIndex ), Span{ offset, count } );
        }
        return true;
    }
} // namespace sw
