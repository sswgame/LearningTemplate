#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLMigrationRunner.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

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

            /** @brief 파일 이름 하나를 해석한 결과입니다. */
            struct MigrationFile
            {
                string _path{};
                string _name{};
                string _driverName{}; ///< 비면 공통 파일이다
                int32  _version{ 0 };
            };

            /** @brief `NNNN_이름.sql` 과 `NNNN_이름.<드라이버>.sql` 을 해석합니다. 형식이 아니면 false 를 반환합니다. */
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

            /** @brief `%#` 서식으로 글을 만듭니다(오류 문구). */
            template <typename... Args>
            static string formatText( string_view format, Args&&... args )
            {
                StringBuilder<constant::kMaxBuffer1024> text;
                text.appendFormat( format, std::forward<Args>( args )... );
                return string( text.view() );
            }

            /** @brief 폴더(리소스 경로나 절대 경로) 아래 파일 경로입니다. 리소스 키는 `/` 로 잇는다. */
            static string joinPath( string_view folder, string_view fileName ) { return formatText( "%#/%#", folder, fileName ); }

            /** @brief 목록 파일(`<SQLMigrationList><Migration _name="NNNN_이름" />…</SQLMigrationList>`)을 읽습니다. 다른 요소 · 속성 · 빈 이름 · 빈 목록은 오류입니다. */
            [[nodiscard]] static bool readList( string_view folder, vector<string>& outListStem, string& outError )
            {
                const string listPath = joinPath( folder, SQLMigrationRunner::kListFileName );
                string       text;
                if ( ResourceIO::readTextResource( listPath, text ) == false )
                {
                    outError = formatText( "cannot read the migration list '%#'", listPath );
                    return false;
                }
                XMLDocument document;
                if ( document.parse( text, listPath ) == false )
                {
                    outError = formatText( "migration list '%#' is not XML: %#", listPath, document.getLastError() );
                    return false;
                }
                const XMLNode root = document.getRoot( "SQLMigrationList", false );
                if ( root.isValid() == false || root.getFirstAttribute().isValid() )
                {
                    outError = formatText( "migration list '%#' must be a <SQLMigrationList> element without attributes", listPath );
                    return false;
                }
                outListStem.clear();
                for ( XMLNode entry = root.findChild(); entry.isValid(); entry = entry.findNextSibling() )
                {
                    const XMLAttribute nameAttribute = entry.getFirstAttribute();
                    const bool         bValidEntry   = string_view{ entry.getName() } == "Migration" && nameAttribute.isValid() && nameAttribute.getNext().isValid() == false &&
                                             string_view{ nameAttribute.getName() } == "_name" && string_view{ nameAttribute.getValue() }.empty() == false;
                    if ( bValidEntry == false )
                    {
                        outError = formatText( "migration list '%#' must hold only <Migration _name=\"NNNN_name\" /> elements", listPath );
                        return false;
                    }
                    outListStem.push_back( nameAttribute.getValue() );
                }
                if ( outListStem.empty() )
                {
                    outError = formatText( "migration list '%#' must hold at least one <Migration>", listPath );
                    return false;
                }
                return true;
            }

            /**
             * @brief 원본 폴더가 디스크에 있으면 그 안의 `.sql` 이 모두 목록의 마이그레이션(공통이나 방언 분기)인지 대조합니다.
             * @details 팩에는 폴더가 없어 대조할 것이 없다 — 팩은 이 폴더에서 만들어지므로 개발 · 테스트 · CI 에서 맞춰 두면 된다.
             */
            [[nodiscard]] static bool checkFolderMatchesList( string_view folder, const vector<string>& listStem, string& outError )
            {
                const string diskFolder = FileUtil::isDirectory( folder ) ? string( folder ) : ResourcePaths::makeAbsolutePath( folder );
                if ( diskFolder.empty() || FileUtil::isDirectory( diskFolder ) == false )
                    return true;
                vector<string> listFilePath;
                if ( FileUtil::collectFiles( diskFolder, ".sql", listFilePath, false ) == false )
                {
                    outError = formatText( "cannot read the migration folder '%#'", diskFolder );
                    return false;
                }
                for ( const string& filePath : listFilePath )
                {
                    const string  fileName = FileUtil::getFileNamePart( filePath );
                    MigrationFile file;
                    if ( parseFileName( fileName, file ) == false )
                    {
                        outError = formatText( "migration file '%#' is not named NNNN_name.sql or NNNN_name.<driver>.sql", fileName );
                        return false;
                    }
                    const string stem    = formatText( "%#%#", string_view( fileName ).substr( 0, static_cast<size_t>( kVersionDigitCount + 1 ) ), file._name );
                    const bool   bListed = VectorUtil::contains( listStem, stem );
                    if ( bListed == false )
                    {
                        outError = formatText( "migration file '%#' is not listed in %# of '%#'", fileName, SQLMigrationRunner::kListFileName, diskFolder );
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SQLMigrationRunner::loadMigrations( string_view folder, string_view driverName, vector<SQLMigration>& outListMigration, string& outError )
    {
        vector<string> listStem;
        if ( SQLMigrationRunnerInternal::readList( folder, listStem, outError ) == false )
            return false;
        if ( SQLMigrationRunnerInternal::checkFolderMatchesList( folder, listStem, outError ) == false )
            return false;
        for ( size_t stemIndex = 0; stemIndex < listStem.size(); ++stemIndex )
        {
            const string&                             stem = listStem[stemIndex];
            SQLMigrationRunnerInternal::MigrationFile file;
            const bool                                bNamed = SQLMigrationRunnerInternal::parseFileName( SQLMigrationRunnerInternal::formatText( "%#.sql", stem ), file ) && file._driverName.empty();
            if ( bNamed == false )
            {
                outError = SQLMigrationRunnerInternal::formatText( "migration '%#' in %# is not named NNNN_name", stem, SQLMigrationRunner::kListFileName );
                return false;
            }
            const int32 expectedVersion = static_cast<int32>( stemIndex ) + 1;
            if ( file._version != expectedVersion )
            {
                outError = SQLMigrationRunnerInternal::makeText( "migration numbers must run 1, 2, 3 ... without gaps - expected ", expectedVersion, " at ", stem );
                return false;
            }
            // 방언 분기 파일이 있으면 공통 파일 대신 쓴다. 다른 드라이버의 방언 파일은 읽지 않는다.
            const string driverPath = SQLMigrationRunnerInternal::joinPath( folder, SQLMigrationRunnerInternal::formatText( "%#.%#.sql", stem, driverName ) );
            const string commonPath = SQLMigrationRunnerInternal::joinPath( folder, SQLMigrationRunnerInternal::formatText( "%#.sql", stem ) );
            const bool   bHasDriver = ResourceIO::hasResource( driverPath );
            if ( bHasDriver == false && ResourceIO::hasResource( commonPath ) == false )
            {
                outError = SQLMigrationRunnerInternal::makeText( "migration ", file._version, " has no common file and no file for driver ", driverName );
                return false;
            }
            const string& path      = bHasDriver ? driverPath : commonPath;
            SQLMigration& migration = outListMigration.emplace_back();
            migration._version      = file._version;
            migration._name         = file._name;
            if ( ResourceIO::readTextResource( path, migration._sql ) == false )
            {
                outError = SQLMigrationRunnerInternal::formatText( "cannot read migration file '%#'", path );
                return false;
            }
            SQLMigrationRunnerInternal::removeCarriageReturns( migration._sql );
        }
        return true;
    }

    bool SQLMigrationRunner::collectDialectDriverNames( string_view folder, vector<string>& inoutListName, string& outError )
    {
        const string diskFolder = FileUtil::isDirectory( folder ) ? string( folder ) : ResourcePaths::makeAbsolutePath( folder );
        if ( diskFolder.empty() || FileUtil::isDirectory( diskFolder ) == false )
            return true;
        vector<string> listFilePath;
        if ( FileUtil::collectFiles( diskFolder, ".sql", listFilePath, false ) == false )
        {
            outError = SQLMigrationRunnerInternal::formatText( "cannot read the migration folder '%#'", diskFolder );
            return false;
        }
        for ( const string& filePath : listFilePath )
        {
            SQLMigrationRunnerInternal::MigrationFile file;
            if ( SQLMigrationRunnerInternal::parseFileName( FileUtil::getFileNamePart( filePath ), file ) == false || file._driverName.empty() )
                continue; // 이름이 틀린 파일은 `loadMigrations` 가 오류로 멈춘다
            if ( VectorUtil::contains( inoutListName, file._driverName ) == false )
                inoutListName.push_back( file._driverName );
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
        return StringUtil::replace( text, "{{autoid}}", dialect._pAutoIDColumn );
    }

    string SQLMigrationRunner::computeChecksum( string_view sql )
    {
        return ServiceKeyUtil::makeHex64( HashUtil::computeStringHash64( sql.data(), sql.size(), false ) );
    }
} // namespace sw
