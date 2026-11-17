#include "pch.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"

#include "Core/Foundation/Constants.h"

namespace sw
{
    ServiceReadSetConnection::ServiceReadSetConnection( IServiceStoreConnection& inner )
        : _inner{ inner }
        , _listRead{}
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
        return _inner.listRecords( table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord );
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
            uint64                 expectedVersion = write._expectedVersion;
            const ReadEntry* const pRead           = expectedVersion == ServiceRecord::kAnyVersion ? findRead( write._table, write._key ) : nullptr;
            if ( pRead != nullptr )
                expectedVersion = pRead->_version; // 읽고 덮어쓰는 쓰기 — 그사이 남이 바꿨으면 다시 읽는다
            switch ( write._kind )
            {
                case ServiceWrite::Kind::Put:
                {
                    conditioned.put( write._table, write._key, vector<uint8>( write._bytes ), expectedVersion );
                    break;
                }
                case ServiceWrite::Kind::Erase:
                {
                    conditioned.erase( write._table, write._key, expectedVersion );
                    break;
                }
                case ServiceWrite::Kind::Require:
                {
                    conditioned.requireVersion( write._table, write._key, expectedVersion );
                    break;
                }
            }
        }
        for ( const ReadEntry& entry : _listRead )
        {
            bool bWritten = false;
            for ( const ServiceWrite& write : listWrite )
            {
                bWritten = bWritten || ( write._table == entry._table && write._key == entry._key );
            }
            if ( bWritten == false )
                conditioned.requireVersion( entry._table, entry._key, entry._version );
        }
        _listRead.clear();
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
