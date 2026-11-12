#include "pch.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceRetentionRule.h"

#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw
{
    ServiceStoreResult ServiceRetentionRule::sweep( IServiceStoreConnection& connection, ServiceRetentionRule& inoutRule, int64 nowMs, int32 maxScanCount, int32 maxDeleteCount,
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
            vector<const ServiceRecord*> listExpired;
            const string*                pLastKey = nullptr;
            for ( const ServiceRecord& record : listRecord )
            {
                if ( outReport._deletedCount + static_cast<int32>( listExpired.size() ) >= maxDeleteCount )
                    break; // 상한에 닿았다. 아직 보지 않은 레코드는 커서 뒤에 남겨 다음 회차가 이어서 본다
                ++outReport._scannedCount;
                pLastKey       = &record._key;
                int64 recordMs = 0;
                if ( inoutRule._pfnReadTime( record, recordMs ) && recordMs <= cutoffMs )
                    listExpired.push_back( &record );
            }
            size_t begin = 0;
            while ( begin < listExpired.size() )
            {
                size_t             count = listExpired.size() - begin;
                ServiceTransaction transaction;
                while ( true )
                {
                    transaction.clear();
                    const vector<const ServiceRecord*> listBatch( listExpired.begin() + static_cast<ptrdiff_t>( begin ),
                                                                  listExpired.begin() + static_cast<ptrdiff_t>( begin + count ) );
                    for ( const ServiceRecord* pRecord : listBatch )
                    {
                        transaction.erase( inoutRule._table, pRecord->_key, pRecord->_version );
                    }
                    if ( inoutRule._pfnStageErase != nullptr )
                    {
                        const ServiceStoreResult staged = inoutRule._pfnStageErase( connection, listBatch, transaction );
                        if ( staged != ServiceStoreResult::Ok )
                            return staged; // 합을 남기지 못하면 지우지 않는다 - 커서도 옮기지 않는다
                    }
                    if ( static_cast<int32>( transaction.getWrites().size() ) <= ServiceTransaction::kMaxWriteCount || count == 1 )
                        break;
                    count = ( count + 1 ) / 2; // 함께 넣는 쓰기가 상한을 넘었다. 묶음을 줄여 다시 짠다
                }
                const ServiceStoreResult committed = connection.commit( transaction );
                if ( committed == ServiceStoreResult::Ok )
                    outReport._deletedCount += static_cast<int32>( count );
                else if ( committed != ServiceStoreResult::Conflict )
                    return committed; // 쓰지 못했으면 커서를 옮기지 않는다 - 옮기면 이 레코드들은 테이블을 한 바퀴 돌 때까지 남는다
                // Conflict: 그사이 바뀐 레코드가 있다. 이 묶음은 건너뛰고 다음 바퀴에 다시 본다
                begin += count;
            }
            if ( pLastKey != nullptr )
                inoutRule._cursor = *pLastKey;
        }
        return ServiceStoreResult::Ok;
    }
} // namespace sw
