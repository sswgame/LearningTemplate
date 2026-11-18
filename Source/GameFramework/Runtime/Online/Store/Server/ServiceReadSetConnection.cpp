#include "pch.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"

#include "Core/Foundation/Constants.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"

namespace sw
{
    ServiceReadSetConnection::ServiceReadSetConnection( IServiceStoreConnection& inner )
        : _inner{ inner }
        , _listRead{}
        , _listRangeRead{}
    {
    }

    ServiceStoreResult ServiceReadSetConnection::readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord )
    {
        const ServiceStoreResult read = _inner.readRecord( table, key, outRecord );
        if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
            return read; // 읽지 못한 것은 판단에 쓰이지 않는다 — 부르는 쪽이 실패로 끝낸다
        const uint64 version = read == ServiceStoreResult::Ok ? outRecord._version : ServiceRecord::kAbsentVersion;
        for ( ReadEntry& entry : _listRead )
        {
            if ( entry._table == table && entry._key == key )
            {
                entry._version = version;
                return read;
            }
        }
        _listRead.push_back( ReadEntry{ table, string( key ), version } );
        return read;
    }

    ServiceStoreResult ServiceReadSetConnection::listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                              vector<ServiceRecord>& outListRecord )
    {
        // 안쪽 연결의 보호 멤버는 기반 참조로 부를 수 없다 — 같은 입구로 넘긴다
        const size_t             first = outListRecord.size();
        const ServiceStoreResult read  = ServiceRecordReader::readPage( _inner, table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord )._result;
        if ( read != ServiceStoreResult::Ok )
            return read; // 읽지 못한 것은 판단에 쓰이지 않는다
        const uint64 fingerprint = ServiceTransaction::computeRangeFingerprint( outListRecord, first );
        const uint8  descending  = bDescending ? SW_TRUE : SW_FALSE;
        for ( RangeEntry& entry : _listRangeRead )
        {
            const bool bSame = entry._table == table && entry._keyPrefix == keyPrefix && entry._cursorKey == cursorKey && entry._maxCount == maxCount && entry._bDescending == descending;
            if ( bSame )
            {
                entry._fingerprint = fingerprint;
                return read;
            }
        }
        _listRangeRead.push_back( RangeEntry{ table, string( keyPrefix ), string( cursorKey ), fingerprint, maxCount, descending } );
        return read;
    }

    ServiceStoreResult ServiceReadSetConnection::commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo )
    {
        const vector<ServiceWrite>& listWrite = transaction.getWrites();
        const auto                  findRead  = [this]( const hashed_string& table, const string& key ) -> const ReadEntry*
        {
            for ( const ReadEntry& entry : _listRead )
            {
                if ( entry._table == table && entry._key == key )
                    return &entry;
            }
            return nullptr;
        };
        ServiceTransaction conditioned;
        for ( const ServiceWrite& write : listWrite )
        {
            ServiceWrite           copy  = write;
            const bool             bKey  = write._kind != ServiceWrite::Kind::RequireRange;
            const ReadEntry* const pRead = bKey && write._expectedVersion == ServiceRecord::kAnyVersion ? findRead( write._table, write._key ) : nullptr;
            if ( pRead != nullptr )
                copy._expectedVersion = pRead->_version; // 읽고 덮어쓰는 쓰기 — 그사이 남이 바꿨으면 다시 읽는다
            conditioned.addWrite( copy );
        }
        for ( const ReadEntry& entry : _listRead )
        {
            bool bWritten = false;
            for ( const ServiceWrite& write : listWrite )
            {
                bWritten = bWritten || ( write._kind != ServiceWrite::Kind::RequireRange && write._table == entry._table && write._key == entry._key );
            }
            if ( bWritten == false )
                conditioned.requireVersion( entry._table, entry._key, entry._version );
        }
        for ( const RangeEntry& entry : _listRangeRead )
        {
            // 안쪽 포장(시도마다 새로 둔 읽기 집합)이 이미 같은 범위를 걸었으면 겹쳐 걸지 않는다
            bool bConditioned = false;
            for ( const ServiceWrite& write : listWrite )
            {
                bConditioned = bConditioned || ( write._kind == ServiceWrite::Kind::RequireRange && write._table == entry._table && write._key == entry._keyPrefix &&
                                                 write._range._cursorKey == entry._cursorKey && write._range._maxCount == entry._maxCount &&
                                                 write._range._bDescending == entry._bDescending );
            }
            if ( bConditioned == false )
                conditioned.requireRange( entry._table, entry._keyPrefix, entry._cursorKey, entry._maxCount, entry._bDescending == SW_TRUE, entry._fingerprint );
        }
        _listRead.clear();
        _listRangeRead.clear();
        ServiceCommitInfo        info;
        const ServiceStoreResult committed = _inner.commit( conditioned, &info );
        if ( pOutInfo != nullptr )
        {
            *pOutInfo = info;
            // 덧붙인 조건이 어긋났으면 부르는 쪽의 트랜잭션에는 그 번호가 없다 — 알 수 없음(-1)으로 준다
            if ( info._conflictIndex >= static_cast<int32>( listWrite.size() ) )
                pOutInfo->_conflictIndex = constant::invalid_index::kInt32;
        }
        return committed;
    }
} // namespace sw
