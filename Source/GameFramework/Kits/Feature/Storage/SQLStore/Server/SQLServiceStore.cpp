#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/SQLServiceStore.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/LogContext.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQLMigrationRunner.h"

namespace sw
{
    namespace
    {
        struct SQLServiceStoreInternal
        {
            static constexpr const utf8* kReadSQL = "SELECT bytes, version FROM sw_record WHERE tbl = ? AND rkey = ?";
            static constexpr const utf8* kListAscendingSQL =
                "SELECT rkey, bytes, version FROM sw_record WHERE tbl = ? AND rkey >= ? AND rkey < ? AND rkey > ? ORDER BY rkey ASC LIMIT ?";
            static constexpr const utf8* kListDescendingSQL =
                "SELECT rkey, bytes, version FROM sw_record WHERE tbl = ? AND rkey >= ? AND rkey < ? ORDER BY rkey DESC LIMIT ?";
            static constexpr const utf8* kInsertAbsentSQL =
                "INSERT INTO sw_record ( tbl, rkey, bytes, version ) VALUES ( ?, ?, ?, ? ) ON CONFLICT ( tbl, rkey ) DO NOTHING";
            static constexpr const utf8* kUpsertSQL          = "INSERT INTO sw_record ( tbl, rkey, bytes, version ) VALUES ( ?, ?, ?, ? ) "
                                                               "ON CONFLICT ( tbl, rkey ) DO UPDATE SET bytes = excluded.bytes, version = excluded.version";
            static constexpr const utf8* kUpdateVersionedSQL = "UPDATE sw_record SET bytes = ?, version = ? WHERE tbl = ? AND rkey = ? AND version = ?";
            static constexpr const utf8* kDeleteSQL          = "DELETE FROM sw_record WHERE tbl = ? AND rkey = ?";
            static constexpr const utf8* kDeleteVersionedSQL = "DELETE FROM sw_record WHERE tbl = ? AND rkey = ? AND version = ?";
            static constexpr const utf8* kVersionSQL         = "SELECT version FROM sw_record WHERE tbl = ? AND rkey = ?";

            static ServiceStoreResult toStoreResult( SQLResult result )
            {
                switch ( result )
                {
                    case SQLResult::Ok:
                        return ServiceStoreResult::Ok;
                    case SQLResult::Constraint:
                    case SQLResult::SerializationFailure:
                        return ServiceStoreResult::Conflict;
                    case SQLResult::Busy:
                    case SQLResult::ConnectionLost:
                    case SQLResult::Error:
                        return ServiceStoreResult::Unavailable;
                }
                return ServiceStoreResult::Unavailable;
            }

            static SQLValue makeTableValue( const hashed_string& table ) { return SQLValue::makeText( string_view( table.c_str(), table.size() ) ); }

            static SQLValue makeBytesValue( const vector<uint8>& bytes ) { return SQLValue::makeBlob( bytes.data(), static_cast<int32>( bytes.size() ) ); }

            /** @brief 워커의 SQL 연결을 일 하나가 도는 동안 `IServiceStoreConnection` 으로 보이게 합니다. */
            class SQLServiceStoreConnection final : public IServiceStoreConnection
            {
            public:
                explicit SQLServiceStoreConnection( ISQLConnection& connection )
                    : _rowSet{}
                    , _connection{ connection }
                {
                }

                ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override
                {
                    outRecord = ServiceRecord{};
                    if ( table.empty() || ServiceTransaction::isValidKey( key ) == false )
                        return ServiceStoreResult::Invalid;
                    const SQLValue  arrParam[] = { makeTableValue( table ), SQLValue::makeText( key ) };
                    const SQLResult result     = _connection.execute( kReadSQL, arrParam, 2, &_rowSet );
                    if ( result != SQLResult::Ok )
                        return ServiceStoreResult::Unavailable; // 읽기에 Conflict 는 없다(계약) - 직렬화 실패 · 교착이 그대로 올라가 호출한 쪽이 커밋이 어긋난 것으로 읽었다
                    if ( _rowSet._listRow.empty() )
                        return ServiceStoreResult::NotFound;
                    outRecord._bytes   = std::move( _rowSet._listRow[0][0]._bytes );
                    outRecord._version = static_cast<uint64>( _rowSet._listRow[0][1]._integer );
                    return ServiceStoreResult::Ok;
                }

                ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                vector<ServiceRecord>& outListRecord ) override
                {
                    if ( table.empty() || maxCount <= 0 )
                        return ServiceStoreResult::Invalid;
                    // 키는 ASCII(키 규칙)이므로 접두어 + 0x7F 가 범위의 끝이다.
                    string upperKey{ keyPrefix };
                    upperKey.push_back( static_cast<utf8>( 0x7F ) );
                    SQLResult result = SQLResult::Ok;
                    if ( bDescending == false )
                    {
                        const SQLValue arrParam[] = { makeTableValue( table ), SQLValue::makeText( keyPrefix ), SQLValue::makeText( upperKey ), SQLValue::makeText( cursorKey ),
                                                      SQLValue::makeInt64( maxCount ) };
                        result                    = _connection.execute( kListAscendingSQL, arrParam, 5, &_rowSet );
                    }
                    else
                    {
                        const string_view upper      = cursorKey.empty() ? string_view{ upperKey } : cursorKey;
                        const SQLValue    arrParam[] = { makeTableValue( table ), SQLValue::makeText( keyPrefix ), SQLValue::makeText( upper ),
                                                         SQLValue::makeInt64( maxCount ) };
                        result                       = _connection.execute( kListDescendingSQL, arrParam, 4, &_rowSet );
                    }
                    if ( result != SQLResult::Ok )
                        return ServiceStoreResult::Unavailable; // 읽기에 Conflict 는 없다(`readRecord` 와 같다)
                    for ( vector<SQLValue>& row : _rowSet._listRow )
                    {
                        ServiceRecord& record = outListRecord.emplace_back();
                        record._key           = string( row[0].getText() );
                        record._bytes         = std::move( row[1]._bytes );
                        record._version       = static_cast<uint64>( row[2]._integer );
                    }
                    return ServiceStoreResult::Ok;
                }

                ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo ) override
                {
                    ServiceCommitInfo info;
                    if ( transaction.isWellFormed() == false )
                        return ServiceStoreResult::Invalid;
                    const SQLResult beginResult = _connection.executeScript( _connection.getDialect()._pBeginWrite );
                    if ( beginResult != SQLResult::Ok )
                        return toStoreResult( beginResult == SQLResult::Constraint ? SQLResult::Error : beginResult );
                    ServiceStoreResult result = applyWrites( transaction, info );
                    if ( result != ServiceStoreResult::Ok )
                    {
                        (void)_connection.executeScript( "ROLLBACK" ); // 이미 실패한 트랜잭션의 정리다 — 원래 오류를 돌려준다
                        if ( pOutInfo != nullptr )
                            *pOutInfo = info;
                        return result;
                    }
                    const SQLResult commitResult = _connection.executeScript( "COMMIT" );
                    if ( commitResult == SQLResult::SerializationFailure )
                    {
                        result              = ServiceStoreResult::Conflict; // PostgreSQL 은 COMMIT 에서 직렬화 실패를 낼 수 있으며, 이때는 적용되지 않은 것이다
                        info._conflictIndex = constant::invalid_index::kInt32;
                        info._commitVersion = 0;
                    }
                    else if ( commitResult != SQLResult::Ok )
                    {
                        result              = ServiceStoreResult::Unavailable; // 적용됐는지 알 수 없다 - 커밋 버전을 알리지 않는다(`_commitVersion` 은 Ok 일 때만 뜻이 있다)
                        info._commitVersion = 0;
                    }
                    // SQLite 의 COMMIT 은 잠금에 지면(SQLITE_BUSY) 트랜잭션을 연 채로 돌아온다. 그대로 두면 이 워커의 다음 BEGIN 이 모두 "트랜잭션 안의 트랜잭션" 으로 진다 - 연결이 살아 있어 다시 열리지도 않는다.
                    if ( commitResult != SQLResult::Ok )
                        (void)_connection.executeScript( "ROLLBACK" ); // 이미 실패한 커밋의 정리다 — 위의 결과를 돌려준다
                    if ( pOutInfo != nullptr )
                        *pOutInfo = info;
                    return result;
                }

            private:
                ServiceStoreResult applyWrites( const ServiceTransaction& transaction, ServiceCommitInfo& outInfo )
                {
                    SQLResult  result                    = _connection.execute( _connection.getDialect()._pNextCommitVersion, nullptr, 0, &_rowSet );
                    const bool bCommitVersionUnavailable = result != SQLResult::Ok || _rowSet._listRow.empty() || _rowSet._listRow[0].empty();
                    if ( bCommitVersionUnavailable )
                        return toStoreResult( result == SQLResult::Ok || result == SQLResult::Constraint ? SQLResult::Error : result );
                    const int64                 commitVersion = _rowSet._listRow[0][0]._integer;
                    const vector<ServiceWrite>& listWrite     = transaction.getWrites();
                    for ( size_t writeIndex = 0; writeIndex < listWrite.size(); ++writeIndex )
                    {
                        const ServiceWrite& write    = listWrite[writeIndex];
                        const SQLValue      table    = makeTableValue( write._table );
                        const SQLValue      key      = SQLValue::makeText( write._key );
                        const bool          bAny     = write._expectedVersion == ServiceRecord::kAnyVersion;
                        const bool          bAbsent  = write._expectedVersion == ServiceRecord::kAbsentVersion;
                        bool                bMatched = true;
                        if ( write._kind == ServiceWrite::Kind::Put && ( bAny || bAbsent ) )
                        {
                            const SQLValue arrParam[] = { table, key, makeBytesValue( write._bytes ), SQLValue::makeInt64( commitVersion ) };
                            result                    = _connection.execute( bAny ? kUpsertSQL : kInsertAbsentSQL, arrParam, 4, &_rowSet );
                            bMatched                  = bAny || _rowSet._affectedRowCount == 1;
                        }
                        else if ( write._kind == ServiceWrite::Kind::Put )
                        {
                            const SQLValue arrParam[] = { makeBytesValue( write._bytes ), SQLValue::makeInt64( commitVersion ), table, key,
                                                          SQLValue::makeInt64( static_cast<int64>( write._expectedVersion ) ) };
                            result                    = _connection.execute( kUpdateVersionedSQL, arrParam, 5, &_rowSet );
                            bMatched                  = _rowSet._affectedRowCount == 1;
                        }
                        else if ( write._kind == ServiceWrite::Kind::Erase && bAny )
                        {
                            const SQLValue arrParam[] = { table, key };
                            result                    = _connection.execute( kDeleteSQL, arrParam, 2, &_rowSet );
                        }
                        else if ( write._kind == ServiceWrite::Kind::Erase && bAbsent == false )
                        {
                            const SQLValue arrParam[] = { table, key, SQLValue::makeInt64( static_cast<int64>( write._expectedVersion ) ) };
                            result                    = _connection.execute( kDeleteVersionedSQL, arrParam, 3, &_rowSet );
                            bMatched                  = _rowSet._affectedRowCount == 1;
                        }
                        else if ( bAny == false ) // Require 와 Erase(없어야 함)는 버전만 확인한다
                        {
                            const SQLValue arrParam[]   = { table, key };
                            result                      = _connection.execute( kVersionSQL, arrParam, 2, &_rowSet );
                            const uint64 currentVersion = _rowSet._listRow.empty() ? ServiceRecord::kAbsentVersion : static_cast<uint64>( _rowSet._listRow[0][0]._integer );
                            bMatched                    = currentVersion == write._expectedVersion;
                        }
                        if ( result != SQLResult::Ok )
                        {
                            if ( result == SQLResult::Constraint || result == SQLResult::SerializationFailure )
                                outInfo._conflictIndex = constant::invalid_index::kInt32;
                            return toStoreResult( result );
                        }
                        if ( bMatched == false )
                        {
                            outInfo._conflictIndex = static_cast<int32>( writeIndex );
                            return ServiceStoreResult::Conflict;
                        }
                    }
                    outInfo._commitVersion = static_cast<uint64>( commitVersion );
                    return ServiceStoreResult::Ok;
                }

            private:
                SQLRowSet       _rowSet;
                ISQLConnection& _connection;
            };

            /** @brief `IServiceStoreJob` 를 풀의 일로 감쌉니다. 맡긴 스레드의 로그 컨텍스트를 저장해 두었다가 워커의 `run` 과 서비스 스레드의 `complete` 에서 다시 적용합니다. */
            class SQLServiceStoreJob final : public ISQLJob
            {
            public:
                explicit SQLServiceStoreJob( unique_ptr<IServiceStoreJob> job )
                    : _job{ std::move( job ) }
                {
                    _job->bindLogContext( LogContext::getCurrent() );
                }

                void run( ISQLConnection& connection ) override
                {
                    ScopedLogContext          scope( _job->getLogContext() );
                    SQLServiceStoreConnection adapter{ connection };
                    _job->run( adapter );
                }

                void cancel() override { _job->markCancelled(); }

                void complete() override
                {
                    ScopedLogContext scope( _job->getLogContext() );
                    _job->complete();
                }

            private:
                unique_ptr<IServiceStoreJob> _job;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SQLServiceStore::SQLServiceStore()
        : _pool{}
    {
    }

    SQLServiceStore::~SQLServiceStore()
    {
        _pool.shutdown();
    }

    expected<void> SQLServiceStore::initialize( string_view driverName, const SQLConnectionPoolConfig& settings, string_view migrationFolder, UnixMilliseconds nowMs )
    {
        const expected<ISQLDriver*> driver = SQLDriverRegistry::findDriver( driverName );
        if ( driver.has_value() == false )
            return unexpected( driver.error() );
        ISQLDriver* const    pDriver = *driver;
        vector<SQLMigration> listMigration;
        if ( expected<void> result = SQLMigrationRunner::loadMigrations( migrationFolder, pDriver->getName(), listMigration ); result.has_value() == false )
            return result;
        expected<unique_ptr<ISQLConnection>> connection = pDriver->openConnection( settings._connection, settings._secret );
        if ( connection.has_value() == false )
            return unexpected( connection.error() );
        if ( expected<void> result = SQLMigrationRunner::apply( **connection, listMigration, nowMs ); result.has_value() == false )
            return result;
        connection->reset();
        return _pool.initialize( pDriver, settings );
    }

    void SQLServiceStore::submit( unique_ptr<IServiceStoreJob> job )
    {
        _pool.submit( sw::make_unique<SQLServiceStoreInternal::SQLServiceStoreJob>( std::move( job ) ) );
    }
} // namespace sw
