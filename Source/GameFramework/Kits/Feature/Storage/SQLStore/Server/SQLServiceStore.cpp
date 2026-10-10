#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/SQLServiceStore.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/LogContext.h"

#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLMigrationRunner.h"

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

            /** @brief 워커의 SQL 연결을 `IServiceStoreConnection` 으로 보입니다(일 하나 동안). */
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
                        return toStoreResult( result == SQLResult::Constraint ? SQLResult::Error : result );
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
                    // 키는 ASCII(키 규칙)라 접두어 + 0x7F 가 범위 끝이다.
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
                        return toStoreResult( result == SQLResult::Constraint ? SQLResult::Error : result );
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
                        (void)_connection.executeScript( "ROLLBACK" );
                        if ( pOutInfo != nullptr )
                            *pOutInfo = info;
                        return result;
                    }
                    const SQLResult commitResult = _connection.executeScript( "COMMIT" );
                    if ( commitResult == SQLResult::SerializationFailure )
                    {
                        result              = ServiceStoreResult::Conflict; // PostgreSQL 은 COMMIT 에서 직렬화 실패를 낼 수 있다 — 적용되지 않았다
                        info._conflictIndex = -1;
                        info._commitVersion = 0;
                    }
                    else if ( commitResult != SQLResult::Ok )
                    {
                        result = ServiceStoreResult::Unavailable; // 적용됐는지 모른다
                    }
                    if ( pOutInfo != nullptr )
                        *pOutInfo = info;
                    return result;
                }

            private:
                ServiceStoreResult applyWrites( const ServiceTransaction& transaction, ServiceCommitInfo& outInfo )
                {
                    SQLResult result = _connection.execute( _connection.getDialect()._pNextCommitVersion, nullptr, 0, &_rowSet );
                    if ( result != SQLResult::Ok || _rowSet._listRow.empty() || _rowSet._listRow[0].empty() )
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
                        else if ( bAny == false ) // Require · Erase(없어야 함) — 판만 본다
                        {
                            const SQLValue arrParam[]   = { table, key };
                            result                      = _connection.execute( kVersionSQL, arrParam, 2, &_rowSet );
                            const uint64 currentVersion = _rowSet._listRow.empty() ? ServiceRecord::kAbsentVersion : static_cast<uint64>( _rowSet._listRow[0][0]._integer );
                            bMatched                    = currentVersion == write._expectedVersion;
                        }
                        if ( result != SQLResult::Ok )
                        {
                            if ( result == SQLResult::Constraint || result == SQLResult::SerializationFailure )
                                outInfo._conflictIndex = -1;
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

                SQLRowSet       _rowSet;
                ISQLConnection& _connection;
            };

            /** @brief `IServiceStoreWork` 를 풀의 일로 감쌉니다. 맡긴 스레드의 로그 문맥을 잡아 워커의 `run` · 서비스 스레드의 `complete` 에 다시 건다. */
            class SQLServiceStoreJob final : public ISQLJob
            {
            public:
                explicit SQLServiceStoreJob( unique_ptr<IServiceStoreWork> work )
                    : _work{ std::move( work ) }
                {
                    _work->bindLogContext( LogContext::getCurrent() );
                }

                void run( ISQLConnection& connection ) override
                {
                    ScopedLogContext          scope( _work->getLogContext() );
                    SQLServiceStoreConnection adapter{ connection };
                    _work->run( adapter );
                }

                void complete() override
                {
                    ScopedLogContext scope( _work->getLogContext() );
                    _work->complete();
                }

            private:
                unique_ptr<IServiceStoreWork> _work;
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

    SQLServiceStore::~SQLServiceStore() { _pool.shutdown(); }

    bool SQLServiceStore::initialize( string_view driverName, const SQLConnectionPoolSettings& settings, string_view migrationFolder, int64 nowMs, string& outError )
    {
        ISQLDriver* pDriver = SQLDriverRegistry::findDriver( driverName, outError );
        if ( pDriver == nullptr )
            return false;
        const string         folderPath = FileUtil::isDirectory( migrationFolder ) ? string( migrationFolder ) : ResourceUtil::makeAbsolutePath( migrationFolder );
        vector<SQLMigration> listMigration;
        if ( SQLMigrationRunner::loadMigrations( folderPath, pDriver->getName(), listMigration, outError ) == false )
            return false;
        unique_ptr<ISQLConnection> connection = pDriver->openConnection( settings._connection, settings._secret, outError );
        if ( connection == nullptr )
            return false;
        if ( SQLMigrationRunner::apply( *connection, listMigration, nowMs, outError ) == false )
            return false;
        connection.reset();
        return _pool.initialize( pDriver, settings, outError );
    }

    void SQLServiceStore::submit( unique_ptr<IServiceStoreWork> work ) { _pool.submit( sw::make_unique<SQLServiceStoreInternal::SQLServiceStoreJob>( std::move( work ) ) ); }
} // namespace sw
