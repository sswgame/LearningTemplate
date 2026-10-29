#include "pch.h"

#include "GameFramework/Base/Online/Store/ServiceRetention.h"

#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "ServiceRetention" );

    ServiceStoreResult ServiceRetention::sweep( IServiceStoreConnection& connection, ServiceRetentionRule& inoutRule, int64 nowMs, int32 maxScanCount, int32 maxDeleteCount,
                                                ServiceRetentionReport& outReport )
    {
        outReport = ServiceRetentionReport{};
        if ( inoutRule._pfnReadTime == nullptr || inoutRule._retentionMs <= 0 )
            return ServiceStoreResult::Invalid;
        const int64 cutoffMs = nowMs - inoutRule._retentionMs;
        while ( outReport._scannedCount < maxScanCount && outReport._deletedCount < maxDeleteCount )
        {
            const int32              pageSize = MathUtil::min( kPageSize, maxScanCount - outReport._scannedCount );
            vector<ServiceRecord>    listRecord;
            const ServiceStoreResult listed = connection.listRecords( inoutRule._table, inoutRule._keyPrefix, inoutRule._cursor, pageSize, false, listRecord );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            if ( listRecord.empty() )
            {
                inoutRule._cursor.clear(); // 끝까지 훑었다. 다음 회차는 처음부터 돈다
                outReport._bWrapped = SW_TRUE;
                break;
            }
            ServiceTransaction transaction;
            int32              stagedCount = 0;
            const string*      pLastKey    = nullptr;
            for ( const ServiceRecord& record : listRecord )
            {
                if ( outReport._deletedCount + stagedCount >= maxDeleteCount )
                    break; // 상한에 닿았다. 아직 보지 않은 레코드는 커서 뒤에 남겨 다음 회차가 이어서 본다
                ++outReport._scannedCount;
                pLastKey       = &record._key;
                int64 recordMs = 0;
                if ( inoutRule._pfnReadTime( record, recordMs ) && recordMs <= cutoffMs )
                {
                    transaction.erase( inoutRule._table, record._key, record._version );
                    ++stagedCount;
                }
            }
            if ( pLastKey != nullptr )
                inoutRule._cursor = *pLastKey;
            if ( stagedCount == 0 )
                continue;
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
                outReport._deletedCount += stagedCount;
            else if ( committed != ServiceStoreResult::Conflict )
                return committed; // 그사이 바뀐 레코드가 있으면 이 쪽은 건너뛰고 다음 회차에 다시 본다
        }
        return ServiceStoreResult::Ok;
    }
} // namespace sw
