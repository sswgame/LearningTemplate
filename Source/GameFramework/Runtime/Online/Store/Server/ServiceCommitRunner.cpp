#include "pch.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"

#include "Core/Log/Logger.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"

namespace sw
{
    ServiceStoreResult ServiceCommitAttempt::commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo )
    {
        return ServiceCommitRunner::commitOnce( _readSet, transaction, pOutInfo );
    }

    ServiceStoreResult ServiceCommitAttempt::listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                          vector<ServiceRecord>& outListRecord )
    {
        return ServiceRecordReader::readPage( _readSet, table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord )._result;
    }

    ServiceStoreResult ServiceCommitRunner::commitOnce( IServiceStoreConnection& connection, const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo )
    {
        return connection.commit( transaction, pOutInfo );
    }

    void ServiceCommitRunner::reportExhausted( const utf8* pName )
    {
        SW_LOG_WARNING_ID( "ServiceStore.ConflictRetriesExhausted", "'%#' conflicted %# times in a row - answered busy, nothing was written", pName, OnlineLimit::kMaxConflictRetry );
    }
} // namespace sw
