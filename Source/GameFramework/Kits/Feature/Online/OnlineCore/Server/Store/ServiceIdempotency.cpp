#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Store/ServiceIdempotency.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    const ServiceOpaqueTable& ServiceIdempotency::getTable()
    {
        static const ServiceOpaqueTable s_table{ "service_idempotency" };
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
            outReplyBytes = ServiceOpaqueTable::takeBytes( record ); // 답 바이트는 그 키트의 응답 형식이다 — 여기서는 뜻을 읽지 않는다
        return result;
    }

    ServiceStoreResult ServiceIdempotency::commit( ServiceCommitAttempt& attempt, ServiceTransaction& inoutTransaction, string_view scope, uint64 keyHigh, uint64 keyLow,
                                                   vector<uint8> replyBytes )
    {
        inoutTransaction.put( getTable(), makeKey( scope, keyHigh, keyLow ), std::move( replyBytes ), ServiceRecord::kAbsentVersion );
        const ServiceStoreResult committed = attempt.commit( inoutTransaction );
        if ( committed != ServiceStoreResult::Unavailable )
            return committed;
        vector<uint8> storedBytes;
        return findReply( attempt, scope, keyHigh, keyLow, storedBytes ) == ServiceStoreResult::Ok ? ServiceStoreResult::Ok : ServiceStoreResult::Unavailable;
    }
} // namespace sw
