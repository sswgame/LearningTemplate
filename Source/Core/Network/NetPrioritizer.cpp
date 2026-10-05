#include "pch.h"

#include "Core/Network/NetPrioritizer.h"

#include <algorithm>

namespace sw
{
    NetPrioritizer::NetPrioritizer()
        : _listEntry{}
        , _listRankScratch{}
    {
    }

    size_t NetPrioritizer::findLowerIndex( uint32 entityId ) const
    {
        size_t low  = 0;
        size_t high = _listEntry.size();
        while ( low < high )
        {
            const size_t middle = ( low + high ) / 2;
            if ( _listEntry[middle]._entityId < entityId )
                low = middle + 1;
            else
                high = middle;
        }
        return low;
    }

    NetPrioritizer::Entry* NetPrioritizer::findEntry( uint32 entityId )
    {
        const size_t index = findLowerIndex( entityId );
        return index < _listEntry.size() && _listEntry[index]._entityId == entityId ? &_listEntry[index] : nullptr;
    }

    void NetPrioritizer::beginAccumulate()
    {
        for ( Entry& entry : _listEntry )
            entry._bTouched = SW_FALSE;
    }

    void NetPrioritizer::accumulate( uint32 entityId, float32 priority, float32 deltaTime )
    {
        const size_t index = findLowerIndex( entityId );
        if ( index == _listEntry.size() || _listEntry[index]._entityId != entityId )
        {
            Entry newEntry;
            newEntry._entityId = entityId;
            _listEntry.insert( _listEntry.begin() + static_cast<ptrdiff_t>( index ), newEntry );
        }
        Entry& entry = _listEntry[index];
        entry._accumulated += priority * deltaTime;
        entry._bTouched = SW_TRUE;
    }

    void NetPrioritizer::removeUntouched()
    {
        _listEntry.erase( std::remove_if( _listEntry.begin(), _listEntry.end(), []( const Entry& entry )
        { return entry._bTouched == SW_FALSE; } ),
                          _listEntry.end() );
    }

    void NetPrioritizer::collectOrder( vector<uint32>& outListEntity )
    {
        _listRankScratch.assign( _listEntry.begin(), _listEntry.end() );
        std::sort( _listRankScratch.begin(), _listRankScratch.end(), []( const Entry& lhs, const Entry& rhs )
        { return lhs._accumulated != rhs._accumulated ? lhs._accumulated > rhs._accumulated : lhs._entityId < rhs._entityId; } );
        outListEntity.resize( _listRankScratch.size() );
        for ( size_t index = 0; index < _listRankScratch.size(); ++index )
            outListEntity[index] = _listRankScratch[index]._entityId;
    }

    void NetPrioritizer::markSent( uint32 entityId )
    {
        Entry* pEntry = findEntry( entityId );
        if ( pEntry != nullptr )
            pEntry->_accumulated = 0.0f;
    }

    void NetPrioritizer::remove( uint32 entityId )
    {
        Entry* pEntry = findEntry( entityId );
        if ( pEntry != nullptr )
            _listEntry.erase( _listEntry.begin() + ( pEntry - _listEntry.data() ) );
    }

    void NetPrioritizer::clear()
    {
        _listEntry.clear();
        _listRankScratch.clear();
    }

    float32 NetPrioritizer::getAccumulated( uint32 entityId ) const
    {
        const size_t index = findLowerIndex( entityId );
        return index < _listEntry.size() && _listEntry[index]._entityId == entityId ? _listEntry[index]._accumulated : 0.0f;
    }
} // namespace sw
