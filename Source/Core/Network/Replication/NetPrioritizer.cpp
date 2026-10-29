#include "pch.h"

#include "Core/Network/Replication/NetPrioritizer.h"

#include "Core/Container/VectorUtil.h"

#include <algorithm>

namespace sw
{
    NetPrioritizer::NetPrioritizer()
        : _listEntry{}
        , _listRankScratch{}
    {
    }

    size_t NetPrioritizer::findLowerIndex( uint32 entityID ) const
    {
        const auto found = std::lower_bound( _listEntry.begin(), _listEntry.end(), entityID, []( const Entry& entry, uint32 id )
        { return entry._entityID < id; } );
        return static_cast<size_t>( found - _listEntry.begin() );
    }

    NetPrioritizer::Entry* NetPrioritizer::findEntry( uint32 entityID )
    {
        const size_t index = findLowerIndex( entityID );
        return index < _listEntry.size() && _listEntry[index]._entityID == entityID ? &_listEntry[index] : nullptr;
    }

    void NetPrioritizer::beginAccumulate()
    {
        for ( Entry& entry : _listEntry )
        {
            entry._bTouched = SW_FALSE;
        }
    }

    void NetPrioritizer::accumulate( uint32 entityID, float32 priority, float32 deltaTime )
    {
        const size_t index = findLowerIndex( entityID );
        if ( index == _listEntry.size() || _listEntry[index]._entityID != entityID )
        {
            Entry newEntry;
            newEntry._entityID = entityID;
            _listEntry.insert( _listEntry.begin() + static_cast<ptrdiff_t>( index ), newEntry );
        }
        Entry& entry = _listEntry[index];
        entry._accumulated += priority * deltaTime;
        entry._bTouched = SW_TRUE;
    }

    void NetPrioritizer::removeUntouched()
    {
        VectorUtil::removeIf( _listEntry, []( const Entry& entry )
        { return entry._bTouched == SW_FALSE; } );
    }

    void NetPrioritizer::collectOrder( vector<uint32>& outListEntity )
    {
        _listRankScratch.assign( _listEntry.begin(), _listEntry.end() );
        VectorUtil::sort( _listRankScratch, []( const Entry& lhs, const Entry& rhs )
        { return lhs._accumulated != rhs._accumulated ? lhs._accumulated > rhs._accumulated : lhs._entityID < rhs._entityID; } );
        outListEntity.resize( _listRankScratch.size() );
        for ( size_t index = 0; index < _listRankScratch.size(); ++index )
        {
            outListEntity[index] = _listRankScratch[index]._entityID;
        }
    }

    void NetPrioritizer::markSent( uint32 entityID )
    {
        Entry* pEntry = findEntry( entityID );
        if ( pEntry == nullptr )
            return;
        pEntry->_accumulated            = 0.0f;
        pEntry->_unconfirmedAccumulated = 0.0f;
        pEntry->_bUnconfirmed           = SW_FALSE;
    }

    void NetPrioritizer::markSentUnconfirmed( uint32 entityID, uint32 sentTick )
    {
        Entry* pEntry = findEntry( entityID );
        if ( pEntry == nullptr )
            return;
        pEntry->_unconfirmedAccumulated += pEntry->_accumulated;
        pEntry->_accumulated     = 0.0f;
        pEntry->_unconfirmedTick = sentTick;
        pEntry->_bUnconfirmed    = SW_TRUE;
    }

    bool NetPrioritizer::findUnconfirmedSendTick( uint32 entityID, uint32& outSentTick ) const
    {
        const size_t index               = findLowerIndex( entityID );
        const bool   bNoUnconfirmedEntry = index == _listEntry.size() || _listEntry[index]._entityID != entityID || _listEntry[index]._bUnconfirmed == SW_FALSE;
        if ( bNoUnconfirmedEntry )
            return false;
        outSentTick = _listEntry[index]._unconfirmedTick;
        return true;
    }

    void NetPrioritizer::resolveSend( uint32 entityID, bool bDelivered )
    {
        Entry* pEntry = findEntry( entityID );
        if ( pEntry == nullptr || pEntry->_bUnconfirmed == SW_FALSE )
            return;
        if ( bDelivered == false )
            pEntry->_accumulated += pEntry->_unconfirmedAccumulated;
        pEntry->_unconfirmedAccumulated = 0.0f;
        pEntry->_bUnconfirmed           = SW_FALSE;
    }

    void NetPrioritizer::remove( uint32 entityID )
    {
        Entry* pEntry = findEntry( entityID );
        if ( pEntry != nullptr )
            _listEntry.erase( _listEntry.begin() + ( pEntry - _listEntry.data() ) );
    }

    void NetPrioritizer::clear()
    {
        _listEntry.clear();
        _listRankScratch.clear();
    }

    float32 NetPrioritizer::getAccumulated( uint32 entityID ) const
    {
        const size_t index = findLowerIndex( entityID );
        return index < _listEntry.size() && _listEntry[index]._entityID == entityID ? _listEntry[index]._accumulated : 0.0f;
    }
} // namespace sw
