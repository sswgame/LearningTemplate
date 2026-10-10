#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLMigrationRunner.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h"

namespace sw
{
    namespace
    {
        struct SQLMigrationRunnerInternal
        {
            static constexpr int32 kVersionDigitCount = 4;

            static constexpr const utf8* kCreateHistorySQL = "CREATE TABLE IF NOT EXISTS sw_schema_migration ( version INTEGER PRIMARY KEY, name TEXT NOT NULL, "
                                                             "checksum TEXT NOT NULL, applied_at_ms BIGINT NOT NULL )";
            static constexpr const utf8* kReadHistorySQL   = "SELECT version, checksum FROM sw_schema_migration";
            static constexpr const utf8* kInsertHistorySQL = "INSERT INTO sw_schema_migration ( version, name, checksum, applied_at_ms ) VALUES ( ?, ?, ?, ? )";

            /** @brief 파일 이름 하나를 읽은 것입니다. */
            struct MigrationFile
            {
                string _path{};
                string _name{};
                string _driverName{}; ///< 비면 공통 파일
                int32  _version{ 0 };
            };

            /** @brief `NNNN_이름.sql` · `NNNN_이름.<드라이버>.sql` 을 읽습니다. 형식이 아니면 false 입니다. */
            [[nodiscard]] static bool parseFileName( string_view fileName, MigrationFile& outFile )
            {
                const string_view suffix = ".sql";
                if ( StringUtil::endsWith( fileName, suffix ) == false )
                    return false;
                string_view stem = fileName.substr( 0, fileName.size() - suffix.size() );
                if ( stem.size() <= static_cast<size_t>( kVersionDigitCount + 1 ) || stem[kVersionDigitCount] != '_' )
                    return false;
                int32 version = 0;
                for ( int32 digitIndex = 0; digitIndex < kVersionDigitCount; ++digitIndex )
                {
                    const utf8 ch = stem[static_cast<size_t>( digitIndex )];
                    if ( ch < '0' || '9' < ch )
                        return false;
                    version = version * 10 + ( ch - '0' );
                }
                stem                = stem.substr( static_cast<size_t>( kVersionDigitCount + 1 ) );
                const size_t dot    = stem.find( '.' );
                outFile._version    = version;
                outFile._name       = string( stem.substr( 0, dot ) );
                outFile._driverName = dot == string_view::npos ? string{} : string( stem.substr( dot + 1 ) );
                return version > 0 && outFile._name.empty() == false;
            }

            static void removeCarriageReturns( string& inoutText )
            {
                string text;
                text.reserve( inoutText.size() );
                for ( const utf8 ch : inoutText )
                {
                    if ( ch != '\r' )
                        text.push_back( ch );
                }
                inoutText.swap( text );
            }

            static void rollback( ISQLConnection& connection ) { (void)connection.executeScript( "ROLLBACK" ); }

            static string makeText( string_view prefix, int32 version, string_view middle, string_view suffix )
            {
                StringBuilder<constant::kMaxBuffer1024> text;
                text.appendFormat( "%#%#%#%#", prefix, version, middle, suffix );
                return string( text.view() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SQLMigrationRunner::loadMigrations( string_view folderPath, string_view driverName, vector<SQLMigration>& outListMigration, string& outError )
    {
        vector<string> listFilePath;
        if ( FileUtil::collectFiles( folderPath, ".sql", listFilePath, false ) == false )
        {
            outError = "cannot read the migration folder '" + string( folderPath ) + "'";
            return false;
        }
        // 번호 → (공통 파일, 이 드라이버 갈래). 다른 드라이버의 갈래는 번호를 차지만 하고 쓰지 않는다.
        map<int32, SQLMigrationRunnerInternal::MigrationFile> mapCommon;
        map<int32, SQLMigrationRunnerInternal::MigrationFile> mapDriver;
        map<int32, bool>                                      mapSeen;
        for ( const string& filePath : listFilePath )
        {
            SQLMigrationRunnerInternal::MigrationFile file;
            const string                              fileName = FileUtil::getFileNamePart( filePath );
            if ( SQLMigrationRunnerInternal::parseFileName( fileName, file ) == false )
            {
                outError = "migration file '" + fileName + "' is not named NNNN_name.sql or NNNN_name.<driver>.sql";
                return false;
            }
            file._path             = filePath;
            mapSeen[file._version] = true;
            if ( file._driverName.empty() == false && file._driverName != driverName )
                continue;
            map<int32, SQLMigrationRunnerInternal::MigrationFile>& mapTarget = file._driverName.empty() ? mapCommon : mapDriver;
            if ( mapTarget.find( file._version ) != mapTarget.end() )
            {
                outError = SQLMigrationRunnerInternal::makeText( "migration ", file._version, " has two files: ", fileName );
                return false;
            }
            mapTarget.emplace( file._version, file );
        }
        int32 expectedVersion = 1;
        for ( const auto& [version, bSeen] : mapSeen )
        {
            (void)bSeen;
            if ( version != expectedVersion )
            {
                outError = SQLMigrationRunnerInternal::makeText( "migration numbers must run 1, 2, 3 ... without gaps - expected ", expectedVersion, " in ", folderPath );
                return false;
            }
            ++expectedVersion;
            const auto driverIt = mapDriver.find( version );
            const auto commonIt = mapCommon.find( version );
            if ( driverIt == mapDriver.end() && commonIt == mapCommon.end() )
            {
                outError = SQLMigrationRunnerInternal::makeText( "migration ", version, " has no common file and no file for driver ", driverName );
                return false;
            }
            const SQLMigrationRunnerInternal::MigrationFile& file      = driverIt != mapDriver.end() ? driverIt->second : commonIt->second;
            SQLMigration&                                    migration = outListMigration.emplace_back();
            migration._version                                         = version;
            migration._name                                            = file._name;
            if ( FileUtil::readTextFile( file._path, migration._sql ) == false )
            {
                outError = "cannot read migration file '" + file._path + "'";
                return false;
            }
            SQLMigrationRunnerInternal::removeCarriageReturns( migration._sql );
        }
        return true;
    }

    bool SQLMigrationRunner::apply( ISQLConnection& connection, const vector<SQLMigration>& listMigration, int64 nowMs, string& outError )
    {
        const SQLDialect& dialect = connection.getDialect();
        if ( connection.executeScript( SQLMigrationRunnerInternal::kCreateHistorySQL ) != SQLResult::Ok )
        {
            outError = string( "cannot create the migration history table: " ) + connection.getLastErrorText();
            return false;
        }
        if ( connection.executeScript( dialect._pBeginWrite ) != SQLResult::Ok )
        {
            outError = string( "cannot begin the migration transaction: " ) + connection.getLastErrorText();
            return false;
        }
        if ( connection.executeScript( dialect._pLockMigrationTable ) != SQLResult::Ok )
        {
            outError = string( "cannot lock the migration history table: " ) + connection.getLastErrorText();
            SQLMigrationRunnerInternal::rollback( connection );
            return false;
        }
        SQLRowSet rowSet;
        if ( connection.execute( SQLMigrationRunnerInternal::kReadHistorySQL, nullptr, 0, &rowSet ) != SQLResult::Ok )
        {
            outError = string( "cannot read the migration history table: " ) + connection.getLastErrorText();
            SQLMigrationRunnerInternal::rollback( connection );
            return false;
        }
        map<int32, string> mapAppliedChecksum;
        for ( const vector<SQLValue>& row : rowSet._listRow )
        {
            mapAppliedChecksum[static_cast<int32>( row[0]._integer )] = string( row[1].getText() );
        }

        for ( const SQLMigration& migration : listMigration )
        {
            const string checksum  = computeChecksum( migration._sql );
            const auto   appliedIt = mapAppliedChecksum.find( migration._version );
            if ( appliedIt != mapAppliedChecksum.end() )
            {
                if ( appliedIt->second == checksum )
                    continue;
                outError = SQLMigrationRunnerInternal::makeText( "migration ", migration._version, " changed after it was applied - add a new migration instead of editing ",
                                                                 migration._name );
                SQLMigrationRunnerInternal::rollback( connection );
                return false;
            }
            if ( connection.executeScript( substituteTokens( migration._sql, dialect ) ) != SQLResult::Ok )
            {
                outError = SQLMigrationRunnerInternal::makeText( "migration ", migration._version, " failed: ", connection.getLastErrorText() );
                SQLMigrationRunnerInternal::rollback( connection );
                return false;
            }
            const SQLValue arrParam[] = { SQLValue::makeInt64( migration._version ), SQLValue::makeText( migration._name ), SQLValue::makeText( checksum ),
                                          SQLValue::makeInt64( nowMs ) };
            if ( connection.execute( SQLMigrationRunnerInternal::kInsertHistorySQL, arrParam, 4, nullptr ) != SQLResult::Ok )
            {
                outError = string( "cannot record a migration: " ) + connection.getLastErrorText();
                SQLMigrationRunnerInternal::rollback( connection );
                return false;
            }
        }
        if ( connection.executeScript( "COMMIT" ) != SQLResult::Ok )
        {
            outError = string( "cannot commit the migrations: " ) + connection.getLastErrorText();
            SQLMigrationRunnerInternal::rollback( connection );
            return false;
        }
        return true;
    }

    string SQLMigrationRunner::substituteTokens( string_view sql, const SQLDialect& dialect )
    {
        string text = StringUtil::replace( sql, "{{blob}}", dialect._pBlobType );
        text        = StringUtil::replace( text, "{{keytext}}", dialect._pKeyTextType );
        return StringUtil::replace( text, "{{autoid}}", dialect._pAutoIdColumn );
    }

    string SQLMigrationRunner::computeChecksum( string_view sql )
    {
        return ServiceKeyUtil::makeHex64( StringUtil::computeHash64( sql.data(), sql.size(), false ) );
    }
} // namespace sw
