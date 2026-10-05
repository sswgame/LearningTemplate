#include "pch.h"

#include "GameFramework/Base/Online/Store/ServiceIdempotency.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    const hashed_string& ServiceIdempotency::getTable()
    {
        static const hashed_string s_table{ "service_idempotency" };
        return s_table;
    }

    string ServiceIdempotency::makeKey( string_view scope, uint64 keyHigh, uint64 keyLow )
    {
        string key{ scope };
        key.push_back( '/' );
        ServiceKeyUtil::appendHex64( key, keyHigh );
        ServiceKeyUtil::appendHex64( key, keyLow );
        return key;
    }

    ServiceStoreResult ServiceIdempotency::findReply( IServiceStoreConnection& connection, string_view scope, uint64 keyHigh, uint64 keyLow, vector<uint8>& outReplyBytes )
    {
        ServiceRecord            record;
        const ServiceStoreResult result = connection.readRecord( getTable(), makeKey( scope, keyHigh, keyLow ), record );
        if ( result == ServiceStoreResult::Ok )
            outReplyBytes = std::move( record._bytes );
        return result;
    }

    void ServiceIdempotency::addReply( ServiceTransaction& transaction, string_view scope, uint64 keyHigh, uint64 keyLow, vector<uint8> replyBytes )
    {
        transaction.put( getTable(), makeKey( scope, keyHigh, keyLow ), std::move( replyBytes ), ServiceRecord::kAbsentVersion );
    }
} // namespace sw
