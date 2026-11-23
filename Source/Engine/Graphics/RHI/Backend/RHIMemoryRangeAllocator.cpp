#include "pch.h"

#include "Engine/Graphics/RHI/Backend/RHIMemoryRangeAllocator.h"

namespace sw
{
    void RHIMemoryRangeAllocator::reset( uint64 sizeBytes )
    {
        _listFree.clear();
        _usedBytes = 0;
        if ( sizeBytes > 0 )
            _listFree.push_back( FreeRange{ 0, sizeBytes } );
    }

    bool RHIMemoryRangeAllocator::allocate( uint64 sizeBytes, uint64 alignment, uint64& outOffset )
    {
        if ( sizeBytes == 0 )
            return false;
        const uint64 step = alignment > 0 ? alignment : 1;
        for ( size_t index = 0; index < _listFree.size(); ++index )
        {
            const FreeRange range   = _listFree[index];
            const uint64    aligned = ( range._offset + step - 1 ) / step * step;
            const uint64    padding = aligned - range._offset;
            if ( padding > range._sizeBytes || range._sizeBytes - padding < sizeBytes )
                continue;
            const FreeRange tail{ aligned + sizeBytes, range._sizeBytes - padding - sizeBytes };
            if ( padding > 0 )
            {
                _listFree[index]._sizeBytes = padding; // 정렬로 남은 앞 조각
                if ( tail._sizeBytes > 0 )
                    _listFree.insert( _listFree.begin() + index + 1, tail );
            }
            else if ( tail._sizeBytes > 0 )
            {
                _listFree[index] = tail;
            }
            else
            {
                _listFree.erase( _listFree.begin() + index );
            }
            _usedBytes += sizeBytes;
            outOffset = aligned;
            return true;
        }
        return false;
    }

    void RHIMemoryRangeAllocator::release( uint64 offset, uint64 sizeBytes )
    {
        if ( sizeBytes == 0 )
            return;
        SW_ASSERT( _usedBytes >= sizeBytes && "memory range released twice" );
        _usedBytes -= sizeBytes;
        size_t index = 0;
        while ( index < _listFree.size() && _listFree[index]._offset < offset )
        {
            ++index;
        }
        const bool bMergePrevious = index > 0 && _listFree[index - 1]._offset + _listFree[index - 1]._sizeBytes == offset;
        const bool bMergeNext     = index < _listFree.size() && offset + sizeBytes == _listFree[index]._offset;
        if ( bMergePrevious && bMergeNext )
        {
            _listFree[index - 1]._sizeBytes += sizeBytes + _listFree[index]._sizeBytes;
            _listFree.erase( _listFree.begin() + index );
        }
        else if ( bMergePrevious )
        {
            _listFree[index - 1]._sizeBytes += sizeBytes;
        }
        else if ( bMergeNext )
        {
            _listFree[index]._offset = offset;
            _listFree[index]._sizeBytes += sizeBytes;
        }
        else
        {
            _listFree.insert( _listFree.begin() + index, FreeRange{ offset, sizeBytes } );
        }
    }
} // namespace sw
