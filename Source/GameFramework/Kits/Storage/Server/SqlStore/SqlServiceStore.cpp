#include "pch.h"

#include "GameFramework/Kits/Storage/Server/SqlStore/SqlServiceStore.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/LogContext.h"

#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Kits/Storage/SqlStore/Sql/SqlDriver.h"
#include "GameFramework/Kits/Storage/SqlStore/Sql/SqlDriverRegistry.h"
#include "GameFramework/Kits/Storage/SqlStore/SqlMigrationRunner.h"

namespace sw
{
    namespace
    {
        struct SqlServiceStoreInternal
        {
            static constexpr const utf8* kReadSql = "SELECT bytes, version FROM sw_record WHERE tbl = ? AND rkey = ?";
            static constexpr const utf8* kListAscendingSql =
                "SELECT rkey, bytes, version FROM sw_record WHERE tbl = ? AND rkey >= ? AND rkey < ? AND rkey > ? ORDER BY rkey ASC LIMIT ?";
            static constexpr const utf8* kListDescendingSql =
                "SELECT rkey, bytes, version FROM sw_record WHERE tbl = ? AND rkey >= ? AND rkey < ? ORDER BY rkey DESC LIMIT ?";
            static constexpr const utf8* kInsertAbsentSql =
                "INSERT INTO sw_record ( tbl, rkey, bytes, version ) VALUES ( ?, ?, ?, ? ) ON CONFLICT ( tbl, rkey ) DO NOTHING";
            static constexpr const utf8* kUpsertSql          = "INSERT INTO sw_record ( tbl, rkey, bytes, version ) VALUES ( ?, ?, ?, ? ) "
                                                               "ON CONFLICT ( tbl, rkey ) DO UPDATE SET bytes = excluded.bytes, version = excluded.version";
            static constexpr const utf8* kUpdateVersionedSql = "UPDATE sw_record SET bytes = ?, version = ? WHERE tbl = ? AND rkey = ? AND version = ?";
            static constexpr const utf8* kDeleteSql          = "DELETE FROM sw_record WHERE tbl = ? AND rkey = ?";
            static constexpr const utf8* kDeleteVersionedSql = "DELETE FROM sw_record WHERE tbl = ? AND rkey = ? AND version = ?";
            static constexpr const utf8* kVersionSql         = "SELECT version FROM sw_record WHERE tbl = ? AND rkey = ?";

            static ServiceStoreResult toStoreResult( SqlResult result )
            {
                switch ( result )
                {
                    case SqlResult::Ok:
                        return ServiceStoreResult::Ok;
                    case SqlResult::Constraint:
                    case SqlResult::SerializationFailure:
                        return ServiceStoreResult::Conflict;
                    case SqlResult::Busy:
                    case SqlResult::ConnectionLost:
                    case SqlResult::Error:
                        return ServiceStoreResult::Unavailable;
                }
                return ServiceStoreResult::Unavailable;
            }

            static SqlValue makeTableValue( const hashed_string& table ) { return SqlValue::makeText( string_view( table.c_str(), table.size() ) ); }

            static SqlValue makeBytesValue( const vector<uint8>& bytes ) { return SqlValue::makeBlob( bytes.data(), static_cast<int32>( bytes.size() ) ); }

            /** @brief 워커의 SQL 연결을 `IServiceStoreConnection` 으로 보입니다(일 하나 동안). */
            class SqlServiceStoreConnection final : public IServiceStoreConnection
            {
            public:
                explicit SqlServiceStoreConnection( ISqlConnection& connection )
                    : _rowSet{}
                    , _connection{ connection }
                {
                }

                ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override
                {
                    outRecord = ServiceRecord{};
                    if ( table.empty() || ServiceTransaction::isValidKey( key ) == false )
                        return ServiceStoreResult::Invalid;
                    const SqlValue  arrParam[] = { makeTableValue( table ), SqlValue::makeText( key ) };
                    const SqlResult result     = _connection.execute( kReadSql, arrParam, 2, &_rowSet );
                    if ( result != SqlResult::Ok )
                        return toStoreResult( result == SqlResult::Constraint ? SqlResult::Error : result );
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
                    SqlResult result = SqlResult::Ok;
                    if ( bDescending == false )
                    {
                        const SqlValue arrParam[] = { makeTableValue( table ), SqlValue::makeText( keyPrefix ), SqlValue::makeText( upperKey ), SqlValue::makeText( cursorKey ),
                                                      SqlValue::makeInt64( maxCount ) };
                        result                    = _connection.execute( kListAscendingSql, arrParam, 5, &_rowSet );
                    }
                    else
                    {
                        const string_view upper      = cursorKey.empty() ? string_view{ upperKey } : cursorKey;
                        const SqlValue    arrParam[] = { makeTableValue( table ), SqlValue::makeText( keyPrefix ), SqlValue::makeText( upper ),
                                                         SqlValue::makeInt64( maxCount ) };
                        result                       = _connection.execute( kListDescendingSql, arrParam, 4, &_rowSet );
                    }
                    if ( result != SqlResult::Ok )
                        return toStoreResult( result == SqlResult::Constraint ? SqlResult::Error : result );
                    for ( vector<SqlValue>& row : _rowSet._listRow )
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
                    const SqlResult beginResult = _connection.executeScript( _connection.getDialect()._pBeginWrite );
                    if ( beginResult != SqlResult::Ok )
                        return toStoreResult( beginResult == SqlResult::Constraint ? SqlResult::Error : beginResult );
                    ServiceStoreResult result = applyWrites( transaction, info );
                    if ( result != ServiceStoreResult::Ok )
                    {
                        (void)_connection.executeScript( "ROLLBACK" );
                        if ( pOutInfo != nullptr )
                            *pOutInfo = info;
                        return result;
                    }
                    const SqlResult commitResult = _connection.executeScript( "COMMIT" );
                    if ( commitResult == SqlResult::SerializationFailure )
                    {
                        result              = ServiceStoreResult::Conflict; // PostgreSQL 은 COMMIT 에서 직렬화 실패를 낼 수 있다 — 적용되지 않았다
                        info._conflictIndex = -1;
                        info._commitVersion = 0;
                    }
                    else if ( commitResult != SqlResult::Ok )
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
                    SqlResult result = _connection.execute( _connection.getDialect()._pNextCommitVersion, nullptr, 0, &_rowSet );
                    if ( result != SqlResult::Ok || _rowSet._listRow.empty() || _rowSet._listRow[0].empty() )
                        return toStoreResult( result == SqlResult::Ok || result == SqlResult::Constraint ? SqlResult::Error : result );
                    const int64                 commitVersion = _rowSet._listRow[0][0]._integer;
                    const vector<ServiceWrite>& listWrite     = transaction.getWrites();
                    for ( size_t writeIndex = 0; writeIndex < listWrite.size(); ++writeIndex )
                    {
                        const ServiceWrite& write    = listWrite[writeIndex];
                        const SqlValue      table    = makeTableValue( write._table );
                        const SqlValue      key      = SqlValue::makeText( write._key );
                        const bool          bAny     = write._expectedVersion == ServiceRecord::kAnyVersion;
                        const bool          bAbsent  = write._expectedVersion == ServiceRecord::kAbsentVersion;
                        bool                bMatched = true;
                        if ( write._kind == ServiceWrite::Kind::Put && ( bAny || bAbsent ) )
                        {
                            const SqlValue arrParam[] = { table, key, makeBytesValue( write._bytes ), SqlValue::makeInt64( commitVersion ) };
                            result                    = _connection.execute( bAny ? kUpsertSql : kInsertAbsentSql, arrParam, 4, &_rowSet );
                            bMatched                  = bAny || _rowSet._affectedRowCount == 1;
                        }
                        else if ( write._kind == ServiceWrite::Kind::Put )
                        {
                            const SqlValue arrParam[] = { makeBytesValue( write._bytes ), SqlValue::makeInt64( commitVersion ), table, key,
                                                          SqlValue::makeInt64( static_cast<int64>( write._expectedVersion ) ) };
                            result                    = _connection.execute( kUpdateVersionedSql, arrParam, 5, &_rowSet );
                            bMatched                  = _rowSet._affectedRowCount == 1;
                        }
                        else if ( write._kind == ServiceWrite::Kind::Erase && bAny )
                        {
                            const SqlValue arrParam[] = { table, key };
                            result                    = _connection.execute( kDeleteSql, arrParam, 2, &_rowSet );
                        }
                        else if ( write._kind == ServiceWrite::Kind::Erase && bAbsent == false )
                        {
                            const SqlValue arrParam[] = { table, key, SqlValue::makeInt64( static_cast<int64>( write._expectedVersion ) ) };
                            result                    = _connection.execute( kDeleteVersionedSql, arrParam, 3, &_rowSet );
                            bMatched                  = _rowSet._affectedRowCount == 1;
                        }
                        else if ( bAny == false ) // Require · Erase(없어야 함) — 판만 본다
                        {
                            const SqlValue arrParam[]   = { table, key };
                            result                      = _connection.execute( kVersionSql, arrParam, 2, &_rowSet );
                            const uint64 currentVersion = _rowSet._listRow.empty() ? ServiceRecord::kAbsentVersion : static_cast<uint64>( _rowSet._listRow[0][0]._integer );
                            bMatched                    = currentVersion == write._expectedVersion;
                        }
                        if ( result != SqlResult::Ok )
                        {
                            if ( result == SqlResult::Constraint || result == SqlResult::SerializationFailure )
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

                SqlRowSet       _rowSet;
                ISqlConnection& _connection;
            };

            /** @brief `IServiceStoreWork` 를 풀의 일로 감쌉니다. 맡긴 스레드의 로그 문맥을 잡아 워커의 `run` · 서비스 스레드의 `complete` 에 다시 건다. */
            class SqlServiceStoreJob final : public ISqlJob
            {
            public:
                explicit SqlServiceStoreJob( unique_ptr<IServiceStoreWork> work )
                    : _work{ std::move( work ) }
                {
                    _work->bindLogContext( LogContext::getCurrent() );
                }

                void run( ISqlConnection& connection ) override
                {
                    ScopedLogContext          scope( _work->getLogContext() );
                    SqlServiceStoreConnection adapter{ connection };
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
    SqlServiceStore::SqlServiceStore()
        : _pool{}
    {
    }

    SqlServiceStore::~SqlServiceStore() { _pool.shutdown(); }

    bool SqlServiceStore::initialize( string_view driverName, const SqlConnectionPoolSettings& settings, string_view migrationFolder, int64 nowMs, string& outError )
    {
        ISqlDriver* pDriver = SqlDriverRegistry::findDriver( driverName, outError );
        if ( pDriver == nullptr )
            return false;
        const string         folderPath = FileUtil::isDirectory( migrationFolder ) ? string( migrationFolder ) : ResourceUtil::makeAbsolutePath( migrationFolder );
        vector<SqlMigration> listMigration;
        if ( SqlMigrationRunner::loadMigrations( folderPath, pDriver->getName(), listMigration, outError ) == false )
            return false;
        unique_ptr<ISqlConnection> connection = pDriver->openConnection( settings._connection, settings._secret, outError );
        if ( connection == nullptr )
            return false;
        if ( SqlMigrationRunner::apply( *connection, listMigration, nowMs, outError ) == false )
            return false;
        connection.reset();
        return _pool.initialize( pDriver, settings, outError );
    }

    void SqlServiceStore::submit( unique_ptr<IServiceStoreWork> work ) { _pool.submit( sw::make_unique<SqlServiceStoreInternal::SqlServiceStoreJob>( std::move( work ) ) ); }
} // namespace sw
