#include "pch.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ServiceTTLRuleInternal
        {
            static constexpr uint8 kBackfillRunning = 1; ///< 진행 값 [1][커서]
            static constexpr uint8 kBackfillDone    = 2; ///< 진행 값 [2] — 다시 훑지 않는다
        };
    } // namespace
} // namespace sw

namespace sw
{
    ServiceStoreResult ServiceTTLRule::sweep( IServiceStoreConnection& connection, ServiceTTLRule& inoutRule, int64 nowMs, int32 maxScanCount, int32 maxDeleteCount,
                                              ServiceTTLSweepReport& outReport )
    {
        outReport = ServiceTTLSweepReport{};
        if ( inoutRule.isValid() == false )
            return ServiceStoreResult::Invalid;
        const int64 cutoffMs = nowMs - inoutRule._expireAfterMs;
        if ( inoutRule._ttlIndexTable.empty() == false )
        {
            if ( inoutRule._pfnBackfillIndexKey != nullptr )
            {
                const ServiceStoreResult backfilled = backfillTTLIndex( connection, inoutRule, maxScanCount, outReport );
                if ( backfilled != ServiceStoreResult::Ok )
                    return backfilled; // 채우지 못한 줄은 다음 회차가 이어서 넣는다 - 범위 읽기는 그 뒤에 해도 늦지 않다
            }
            return sweepTTLIndex( connection, inoutRule, cutoffMs, maxScanCount, maxDeleteCount, outReport );
        }
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

    bool ServiceTTLRule::isValid() const
    {
        return _expireAfterMs > 0 && ( _pfnReadTime != nullptr || _ttlIndexTable.empty() == false );
    }

    bool ServiceTTLRule::isSchedulable() const
    {
        return isValid() && _maxScanPerSweep > 0 && _maxDeletePerSweep > 0;
    }

    const hashed_string& ServiceTTLRule::getBackfillTable()
    {
        static const hashed_string s_table{ "service_ttl_backfill" };
        return s_table;
    }

    ServiceStoreResult ServiceTTLRule::backfillTTLIndex( IServiceStoreConnection& connection, const ServiceTTLRule& rule, int32 maxScanCount, ServiceTTLSweepReport& inoutReport )
    {
        using Internal = ServiceTTLRuleInternal;
        string progressKey{ rule._table.viewOrEmpty() };
        progressKey.push_back( '/' );
        progressKey += rule._keyPrefix;
        int32 scannedCount = 0;
        while ( scannedCount < maxScanCount )
        {
            ServiceRecord            progress;
            const ServiceStoreResult read = connection.readRecord( getBackfillTable(), progressKey, progress );
            if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                return read;
            if ( read == ServiceStoreResult::Ok && ( progress._bytes.empty() || progress._bytes[0] != Internal::kBackfillRunning ) )
                return ServiceStoreResult::Ok; // 끝났다. 모르는 값도 다시 훑지 않는다 - 줄이 빠진 레코드는 남는 쪽으로 틀린다
            string cursor;
            for ( size_t index = 1; index < progress._bytes.size(); ++index )
            {
                cursor.push_back( static_cast<utf8>( progress._bytes[index] ) );
            }
            // 한 트랜잭션에 진행 줄 하나가 함께 들어가므로 쓰기 상한보다 하나 적게 읽는다
            const int32              pageSize = MathUtil::min( ServiceTransaction::kMaxWriteCount - 1, maxScanCount - scannedCount );
            vector<ServiceRecord>    listRecord;
            const ServiceStoreResult listed = connection.listRecords( rule._table, rule._keyPrefix, cursor, pageSize, false, listRecord );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            ServiceTransaction transaction;
            int32              indexedCount = 0;
            for ( const ServiceRecord& record : listRecord )
            {
                string indexKey;
                if ( rule._pfnBackfillIndexKey( record, indexKey ) == false )
                    continue; // 기준 시각이 아직 없는 레코드(열린 거래 · 열린 신고)다. 시각이 생기는 쓰기가 줄을 넣는다
                transaction.put( rule._ttlIndexTable, indexKey, vector<uint8>{}, ServiceRecord::kAnyVersion );
                ++indexedCount;
            }
            scannedCount += static_cast<int32>( listRecord.size() );
            const bool    bDone = static_cast<int32>( listRecord.size() ) < pageSize;
            vector<uint8> progressBytes{ bDone ? Internal::kBackfillDone : Internal::kBackfillRunning };
            if ( bDone == false )
            {
                for ( const utf8 letter : listRecord.back()._key )
                {
                    progressBytes.push_back( static_cast<uint8>( letter ) );
                }
            }
            transaction.put( getBackfillTable(), progressKey, std::move( progressBytes ), progress._version ); // 없으면 "없어야 함"이다
            const ServiceStoreResult committed = connection.commit( transaction );
            if ( committed == ServiceStoreResult::Conflict )
                return ServiceStoreResult::Ok; // 다른 쓸기가 같은 진행을 옮겼다. 다음 회차가 이어 간다
            if ( committed != ServiceStoreResult::Ok )
                return committed;
            inoutReport._backfilledCount += indexedCount;
            if ( bDone )
                return ServiceStoreResult::Ok;
        }
        return ServiceStoreResult::Ok;
    }

    string ServiceTTLRule::makeTTLIndexKey( int64 timeMs, string_view target )
    {
        string key;
        ServiceKeyUtil::appendHex64( key, static_cast<uint64>( timeMs < 0 ? 0 : timeMs ) );
        key.push_back( '/' );
        key += target;
        return key;
    }

    bool ServiceTTLRule::parseTTLIndexKey( string_view indexKey, int64& outTimeMs, string_view& outTarget )
    {
        const size_t width  = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
        uint64       timeMs = 0;
        if ( indexKey.size() <= width + 1 || indexKey[width] != '/' || ServiceKeyUtil::parseHex64( indexKey.substr( 0, width ), timeMs ) == false )
            return false;
        outTimeMs = static_cast<int64>( timeMs );
        outTarget = indexKey.substr( width + 1 );
        return true;
    }

    ServiceStoreResult ServiceTTLRule::sweepTTLIndex( IServiceStoreConnection& connection, const ServiceTTLRule& rule, int64 cutoffMs, int32 maxScanCount,
                                                      int32 maxDeleteCount, ServiceTTLSweepReport& outReport )
    {
        string cursor; // 회차 안에서만 쓴다 - Conflict 로 남긴 줄을 이번 회차에 다시 보지 않으려는 것이다
        while ( outReport._scannedCount < maxScanCount && outReport._deletedCount < maxDeleteCount )
        {
            const int32              pageSize = MathUtil::min( kPageSize, maxScanCount - outReport._scannedCount );
            vector<ServiceRecord>    listEntry;
            const ServiceStoreResult listed = connection.listRecords( rule._ttlIndexTable, "", cursor, pageSize, false, listEntry );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            if ( listEntry.empty() )
            {
                outReport._bWrapped = SW_TRUE;
                return ServiceStoreResult::Ok;
            }
            for ( const ServiceRecord& entry : listEntry )
            {
                if ( outReport._deletedCount >= maxDeleteCount )
                    return ServiceStoreResult::Ok;
                int64       entryMs = 0;
                string_view target;
                const bool  bParsed = parseTTLIndexKey( entry._key, entryMs, target );
                if ( bParsed && entryMs > cutoffMs )
                    return ServiceStoreResult::Ok; // 시각 순이다. 여기부터는 아직 기간 안이다
                ++outReport._scannedCount;
                const string previous = cursor;
                cursor                = entry._key;
                if ( bParsed == false )
                {
                    // 색인 형식이 틀린 줄이다. 데이터가 아니라 색인이므로 지워도 잃는 것이 없고, 남기면 앞자리를 막는다
                    ServiceTransaction drop;
                    drop.erase( rule._ttlIndexTable, entry._key, entry._version );
                    const ServiceStoreResult dropped = connection.commit( drop );
                    if ( dropped != ServiceStoreResult::Ok && dropped != ServiceStoreResult::Conflict )
                        return dropped;
                    continue;
                }
                bool                     bEntryDone = true;
                const ServiceStoreResult expired    = expireTTLIndexEntry( connection, rule, entry, target, cutoffMs, outReport, bEntryDone );
                if ( expired != ServiceStoreResult::Ok && expired != ServiceStoreResult::Conflict )
                    return expired; // Conflict 는 다음 회차가 맨 앞에서 다시 본다
                if ( expired == ServiceStoreResult::Ok && bEntryDone == false )
                {
                    cursor = previous; // 묶음이 한 쪽보다 크다. 같은 줄을 다시 읽어 이어서 지운다(지운 만큼 줄었으므로 끝난다)
                    break;
                }
            }
        }
        return ServiceStoreResult::Ok;
    }

    ServiceStoreResult ServiceTTLRule::expireTTLIndexEntry( IServiceStoreConnection& connection, const ServiceTTLRule& rule, const ServiceRecord& entry,
                                                            string_view target, int64 cutoffMs, ServiceTTLSweepReport& inoutReport, bool& outbEntryDone )
    {
        vector<ServiceRecord> listTarget;
        bool                  bEntryDone = true; // 색인 줄도 지울지다
        if ( target.back() == '/' )
        {
            // 접두어 묶음: 한 트랜잭션에 색인 줄 하나가 함께 들어가므로 쓰기 상한보다 하나 적게 읽는다
            constexpr int32          kGroupPageSize = ServiceTransaction::kMaxWriteCount - 1;
            const ServiceStoreResult listed         = connection.listRecords( rule._table, target, "", kGroupPageSize, false, listTarget );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            const bool bFullPage    = static_cast<int32>( listTarget.size() ) == kGroupPageSize;
            size_t     expiredCount = 0;
            for ( const ServiceRecord& record : listTarget )
            {
                int64 recordMs = 0;
                if ( rule._pfnReadTime != nullptr && ( rule._pfnReadTime( record, recordMs ) == false || recordMs > cutoffMs ) )
                    break; // 키 순서가 시각 순서다. 여기부터 남는다(읽지 못하는 것도 남긴다)
                ++expiredCount;
            }
            bEntryDone = expiredCount < listTarget.size() || bFullPage == false;
            listTarget.resize( expiredCount );
        }
        else
        {
            ServiceRecord            record;
            const ServiceStoreResult read = connection.readRecord( rule._table, target, record );
            if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                return read;
            if ( read == ServiceStoreResult::Ok )
            {
                record._key         = string( target ); // 읽기는 키를 채우지 않는다. 시각 읽기가 키를 볼 수 있다
                int64      recordMs = 0;
                const bool bExpired = rule._pfnReadTime == nullptr || ( rule._pfnReadTime( record, recordMs ) && recordMs <= cutoffMs );
                if ( bExpired )
                    listTarget.push_back( std::move( record ) );
            }
            // 없거나, 다시 써서 시각이 늦춰졌거나(새 색인 줄이 있다), 읽지 못하면 레코드는 두고 이 색인 줄만 지운다
        }
        ServiceTransaction           transaction;
        vector<const ServiceRecord*> listErased;
        for ( const ServiceRecord& record : listTarget )
        {
            transaction.erase( rule._table, record._key, record._version );
            listErased.push_back( &record );
        }
        if ( bEntryDone )
            transaction.erase( rule._ttlIndexTable, entry._key, entry._version );
        if ( rule._pfnStageErase != nullptr && listErased.empty() == false )
        {
            const ServiceStoreResult staged = rule._pfnStageErase( connection, listErased, transaction );
            if ( staged != ServiceStoreResult::Ok )
                return staged;
        }
        if ( transaction.isEmpty() )
            return ServiceStoreResult::Ok;
        const ServiceStoreResult committed = connection.commit( transaction );
        if ( committed == ServiceStoreResult::Ok )
            inoutReport._deletedCount += static_cast<int32>( listErased.size() );
        outbEntryDone = bEntryDone;
        return committed;
    }
} // namespace sw
