#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SqlStore/SqlLocalSlotStorage.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Sql/SqlDriver.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Sql/SqlDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/SqlMigrationRunner.h"

namespace sw
{
    namespace
    {
        struct SqlLocalSlotStorageInternal
        {
            /** @brief 로컬 슬롯이 쓰는 드라이버의 등록 이름 — 제품은 드라이버 폴더만 안다(등록부 `SqlDriverRegistry` 가 고른다). */
            static constexpr string_view kLocalSlotDriverName = "sqlite";

            static int64 makeNowMs() { return FileUtil::getCurrentFileWriteTime() / ( FileUtil::kFileTimeTicksPerSecond / 1000 ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SqlLocalSlotStorage::SqlLocalSlotStorage()
        : _connection{}
    {
    }

    SqlLocalSlotStorage::~SqlLocalSlotStorage() = default;

    bool SqlLocalSlotStorage::initialize( string_view databasePath, string& outError )
    {
        ISqlDriver* pDriver = SqlDriverRegistry::findDriver( SqlLocalSlotStorageInternal::kLocalSlotDriverName, outError );
        if ( pDriver == nullptr )
            return false;
        ISqlDriver&          driver     = *pDriver;
        const string         folderPath = ResourceUtil::makeAbsolutePath( kMigrationFolder );
        vector<SqlMigration> listMigration;
        if ( SqlMigrationRunner::loadMigrations( folderPath, driver.getName(), listMigration, outError ) == false )
            return false;
        if ( databasePath != ":memory:" )
            (void)FileUtil::ensureParentDirectoryExists( databasePath );
        _connection = driver.openConnection( databasePath, "", outError );
        if ( _connection == nullptr )
            return false;
        if ( SqlMigrationRunner::apply( *_connection, listMigration, SqlLocalSlotStorageInternal::makeNowMs(), outError ) == false )
        {
            _connection.reset();
            return false;
        }
        return true;
    }

    LocalStoreResult SqlLocalSlotStorage::readSlot( const string& slot, vector<uint8>& outEnvelopeBytes )
    {
        const SqlValue arrParam[1] = { SqlValue::makeText( slot ) };
        SqlRowSet      rowSet;
        if ( _connection->execute( "SELECT bytes FROM sw_local_slot WHERE slot = ?", arrParam, 1, &rowSet ) != SqlResult::Ok )
            return LocalStoreResult::IoError;
        if ( rowSet._listRow.empty() )
            return LocalStoreResult::NotFound;
        outEnvelopeBytes = std::move( rowSet._listRow[0][0]._bytes );
        return LocalStoreResult::Ok;
    }

    LocalStoreResult SqlLocalSlotStorage::writeSlot( const string& slot, const vector<uint8>& envelopeBytes )
    {
        const SqlValue  arrParam[3] = { SqlValue::makeText( slot ), SqlValue::makeBlob( envelopeBytes.data(), static_cast<int32>( envelopeBytes.size() ) ),
                                        SqlValue::makeInt64( SqlLocalSlotStorageInternal::makeNowMs() ) };
        const SqlResult result      = _connection->execute( "INSERT INTO sw_local_slot ( slot, bytes, written_at_ms ) VALUES ( ?, ?, ? ) "
                                                                 "ON CONFLICT ( slot ) DO UPDATE SET bytes = excluded.bytes, written_at_ms = excluded.written_at_ms",
                                                            arrParam, 3, nullptr );
        return result == SqlResult::Ok ? LocalStoreResult::Ok : LocalStoreResult::IoError;
    }

    LocalStoreResult SqlLocalSlotStorage::eraseSlot( const string& slot )
    {
        const SqlValue arrParam[1] = { SqlValue::makeText( slot ) };
        SqlRowSet      rowSet;
        if ( _connection->execute( "DELETE FROM sw_local_slot WHERE slot = ?", arrParam, 1, &rowSet ) != SqlResult::Ok )
            return LocalStoreResult::IoError;
        return rowSet._affectedRowCount > 0 ? LocalStoreResult::Ok : LocalStoreResult::NotFound;
    }

    LocalStoreResult SqlLocalSlotStorage::listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo )
    {
        // 접두는 슬롯 규칙의 글자뿐이라 LIKE 의 와일드카드(% _)를 피해 범위로 묻는다: [접두, 접두 + 0x7F).
        const string   upperBound  = groupPrefix + static_cast<utf8>( 0x7F );
        const SqlValue arrParam[2] = { SqlValue::makeText( groupPrefix ), SqlValue::makeText( upperBound ) };
        SqlRowSet      rowSet;
        if ( _connection->execute( "SELECT slot, length( bytes ), written_at_ms FROM sw_local_slot WHERE slot >= ? AND slot < ? ORDER BY slot", arrParam, 2, &rowSet ) !=
             SqlResult::Ok )
            return LocalStoreResult::IoError;
        for ( const vector<SqlValue>& row : rowSet._listRow )
        {
            LocalSlotInfo& info = outListSlotInfo.emplace_back();
            info._slot          = string( row[0].getText() );
            info._byteCount     = row[1]._integer;
            info._writtenAtMs   = row[2]._integer;
        }
        return LocalStoreResult::Ok;
    }

    bool SqlLocalSlotStorage::registerLocalStoreBackend() { return LocalStoreFactory::registerBackend( kBackendName, &SqlLocalSlotStorage::createForRoot ); }

    void SqlLocalSlotStorage::unregisterLocalStoreBackend() { LocalStoreFactory::unregisterBackend( kBackendName ); }

    unique_ptr<ILocalSlotStorage> SqlLocalSlotStorage::createForRoot( string_view rootPath, string& outError )
    {
        unique_ptr<SqlLocalSlotStorage> storage = sw::make_unique<SqlLocalSlotStorage>();
        if ( storage->initialize( FileUtil::joinPath( rootPath, kDatabaseFileName ), outError ) == false )
            return nullptr;
        return storage;
    }
} // namespace sw
