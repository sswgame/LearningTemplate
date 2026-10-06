#include "pch.h"

#include "GameFramework/Base/Online/Service/ServicePendingTable.h"

namespace sw
{
    ServicePendingTable::ServicePendingTable()
        : _mapTagToToken{}
        , _nextTag{ 1 }
    {
    }

    uint64 ServicePendingTable::add( const NetRequestToken& token )
    {
        const uint64 requestTag = _nextTag++;
        _mapTagToToken.emplace( requestTag, token );
        return requestTag;
    }

    bool ServicePendingTable::take( uint64 requestTag, NetRequestToken& outToken )
    {
        const auto tokenIt = _mapTagToToken.find( requestTag );
        if ( tokenIt == _mapTagToToken.end() )
            return false;
        outToken = tokenIt->second;
        _mapTagToToken.erase( tokenIt );
        return true;
    }
} // namespace sw
