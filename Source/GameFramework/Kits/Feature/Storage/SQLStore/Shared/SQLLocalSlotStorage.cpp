#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLLocalSlotStorage.h"

#include "Core/File/FileUtil.h"

#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLMigrationRunner.h"

namespace sw
{
    namespace
    {
        struct SQLLocalSlotStorageInternal
        {
            /** @brief 로컬 슬롯이 쓰는 드라이버의 등록 이름입니다. 제품 이름은 드라이버 폴더만 알며, 레지스트리(`SQLDriverRegistry`)가 드라이버를 고릅니다. */
            static constexpr string_view kLocalSlotDriverName = "sqlite";

            static int64 makeNowMs() { return FileUtil::getCurrentFileWriteTime() / ( FileUtil::kFileTimeTicksPerSecond / constant::kMillisecondsPerSecond ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SQLLocalSlotStorage::SQLLocalSlotStorage()
        : _connection{}
    {
    }

    SQLLocalSlotStorage::~SQLLocalSlotStorage() = default;

    bool SQLLocalSlotStorage::initialize( string_view databasePath, string& outError )
    {
        ISQLDriver* pDriver = SQLDriverRegistry::findDriver( SQLLocalSlotStorageInternal::kLocalSlotDriverName, outError );
        if ( pDriver == nullptr )
            return false;
        ISQLDriver&          driver = *pDriver;
        vector<SQLMigration> listMigration;
        if ( SQLMigrationRunner::loadMigrations( kMigrationFolder, driver.getName(), listMigration, outError ) == false )
            return false;
        if ( databasePath != ":memory:" )
            (void)FileUtil::ensureParentDirectoryExists( databasePath );
        _connection = driver.openConnection( databasePath, "", outError );
        if ( _connection == nullptr )
            return false;
        if ( SQLMigrationRunner::apply( *_connection, listMigration, SQLLocalSlotStorageInternal::makeNowMs(), outError ) == false )
        {
            _connection.reset();
            return false;
        }
        return true;
    }

    LocalStoreResult SQLLocalSlotStorage::readSlot( const string& slot, vector<uint8>& outEnvelopeBytes )
    {
        const SQLValue arrParam[1] = { SQLValue::makeText( slot ) };
        SQLRowSet      rowSet;
        if ( _connection->execute( "SELECT bytes FROM sw_local_slot WHERE slot = ?", arrParam, 1, &rowSet ) != SQLResult::Ok )
            return LocalStoreResult::IOError;
        if ( rowSet._listRow.empty() )
            return LocalStoreResult::NotFound;
        outEnvelopeBytes = std::move( rowSet._listRow[0][0]._bytes );
        return LocalStoreResult::Ok;
    }

    LocalStoreResult SQLLocalSlotStorage::writeSlot( const string& slot, const vector<uint8>& envelopeBytes )
    {
        const SQLValue  arrParam[3] = { SQLValue::makeText( slot ), SQLValue::makeBlob( envelopeBytes.data(), static_cast<int32>( envelopeBytes.size() ) ),
                                        SQLValue::makeInt64( SQLLocalSlotStorageInternal::makeNowMs() ) };
        const SQLResult result      = _connection->execute( "INSERT INTO sw_local_slot ( slot, bytes, written_at_ms ) VALUES ( ?, ?, ? ) "
                                                                 "ON CONFLICT ( slot ) DO UPDATE SET bytes = excluded.bytes, written_at_ms = excluded.written_at_ms",
                                                            arrParam, 3, nullptr );
        return result == SQLResult::Ok ? LocalStoreResult::Ok : LocalStoreResult::IOError;
    }

    LocalStoreResult SQLLocalSlotStorage::eraseSlot( const string& slot )
    {
        const SQLValue arrParam[1] = { SQLValue::makeText( slot ) };
        SQLRowSet      rowSet;
        if ( _connection->execute( "DELETE FROM sw_local_slot WHERE slot = ?", arrParam, 1, &rowSet ) != SQLResult::Ok )
            return LocalStoreResult::IOError;
        return rowSet._affectedRowCount > 0 ? LocalStoreResult::Ok : LocalStoreResult::NotFound;
    }

    LocalStoreResult SQLLocalSlotStorage::listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo )
    {
        // 접두어는 슬롯 규칙에 맞는 글자뿐이므로 LIKE 의 와일드카드(% _)를 피해 범위로 조회한다: [접두어, 접두어 + 0x7F).
        const string   upperBound  = groupPrefix + static_cast<utf8>( 0x7F );
        const SQLValue arrParam[2] = { SQLValue::makeText( groupPrefix ), SQLValue::makeText( upperBound ) };
        SQLRowSet      rowSet;
        if ( _connection->execute( "SELECT slot, length( bytes ), written_at_ms FROM sw_local_slot WHERE slot >= ? AND slot < ? ORDER BY slot", arrParam, 2, &rowSet ) !=
             SQLResult::Ok )
            return LocalStoreResult::IOError;
        for ( const vector<SQLValue>& row : rowSet._listRow )
        {
            LocalSlotInfo& slotInfo = outListSlotInfo.emplace_back();
            slotInfo._slot          = string( row[0].getText() );
            slotInfo._byteCount     = row[1]._integer;
            slotInfo._writtenAtMs   = row[2]._integer;
        }
        return LocalStoreResult::Ok;
    }

    unique_ptr<ILocalSlotStorage> SQLLocalSlotStorage::createForRoot( string_view rootPath, string& outError )
    {
        unique_ptr<SQLLocalSlotStorage> storage = sw::make_unique<SQLLocalSlotStorage>();
        if ( storage->initialize( FileUtil::joinPath( rootPath, kDatabaseFileName ), outError ) == false )
            return nullptr;
        return storage;
    }
} // namespace sw

namespace sw
{
    // 모듈이 로드되면 "sqlite" 바닥을 기반 공장에 올리고, 언로드되면 내린다.
    const LocalStoreBackendRegistration s_sqliteLocalStoreBackendRegistration{ SQLLocalSlotStorage::kBackendName, &SQLLocalSlotStorage::createForRoot };
} // namespace sw
