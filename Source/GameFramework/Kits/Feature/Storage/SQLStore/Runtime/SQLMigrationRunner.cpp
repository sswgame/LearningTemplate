#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQLMigrationRunner.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

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

            static void rollback( ISQLConnection& connection ) { (void)connection.executeScript( "ROLLBACK" ); } // 이미 실패한 트랜잭션의 정리다 — 원래 오류를 돌려준다

            /** @brief 대상이 "<번호> <무엇>" 인 마이그레이션 오류입니다. */
            static Error makeError( ErrorKind kind, const utf8* pMessage, int32 version, string_view what )
            {
                StringBuilder<constant::buffer::kMax256> detailBuilder;
                detailBuilder.appendFormat( "%# %#", version, what );
                return Error{ kind, pMessage, detailBuilder.view() };
            }

            /** @brief 목록 파일(`<SQLMigrationList><Migration _name="NNNN_이름" />…</SQLMigrationList>`)을 읽습니다. 다른 요소 · 속성 · 빈 이름 · 빈 목록은 오류입니다. */
            [[nodiscard]] static expected<void> readList( string_view folder, vector<string>& outListStem )
            {
                const string listPath = FileUtil::joinPath( folder, SQLMigrationRunner::kListFileName );
                string       text;
                if ( AssetIO::readTextAsset( AssetID{ listPath }, text ) == false )
                {
                    StringBuilder<constant::buffer::kMax128> errorBuilder;
                    errorBuilder.appendFormat( "%#", listPath );
                    return unexpected( Error{ ErrorKind::InvalidArgument, "cannot read the migration list", errorBuilder.view() } );
                }
                XMLDocument document;
                if ( document.parse( text, listPath ) == false )
                {
                    StringBuilder<constant::buffer::kMax128> errorBuilder;
                    errorBuilder.appendFormat( "%#, %#", listPath, document.getLastError() );
                    return unexpected( Error{ ErrorKind::InvalidArgument, "migration list is not XML", errorBuilder.view() } );
                }
                const XMLNode root = document.getRoot( "SQLMigrationList" );
                if ( root.isValid() == false || root.getFirstAttribute().isValid() )
                {
                    StringBuilder<constant::buffer::kMax128> errorBuilder;
                    errorBuilder.appendFormat( "%#", listPath );
                    return unexpected( Error{ ErrorKind::InvalidArgument, "migration list must be a <SQLMigrationList> element without attributes", errorBuilder.view() } );
                }
                outListStem.clear();
                for ( XMLNode entry = root.findChild(); entry.isValid(); entry = entry.findNextSibling() )
                {
                    const XMLAttribute nameAttribute = entry.getFirstAttribute();
                    const bool         bValidEntry   = string_view{ entry.getName() } == "Migration" && nameAttribute.isValid() && nameAttribute.getNext().isValid() == false &&
                                             string_view{ nameAttribute.getName() } == "_name" && string_view{ nameAttribute.getValue() }.empty() == false;
                    if ( bValidEntry == false )
                    {
                        StringBuilder<constant::buffer::kMax128> errorBuilder;
                        errorBuilder.appendFormat( "%#", listPath );
                        return unexpected( Error{ ErrorKind::InvalidArgument, "migration list must hold only <Migration _name=\"NNNN_name\" /> elements", errorBuilder.view() } );
                    }
                    outListStem.push_back( nameAttribute.getValue() );
                }
                if ( outListStem.empty() )
                {
                    StringBuilder<constant::buffer::kMax128> errorBuilder;
                    errorBuilder.appendFormat( "%#", listPath );
                    return unexpected( Error{ ErrorKind::InvalidArgument, "migration list must hold at least one <Migration>", errorBuilder.view() } );
                }
                return {};
            }

            /**
             * @brief 폴더(리소스 id)의 `.sql` 을 읽기와 같은 규칙(`AssetIO::collectAssets` - 낱개 파일 · 팩)으로 나열합니다. 폴더가 없으면 빈 목록이고 true 입니다.
             * @details 예전에는 글이 디스크 폴더인지 먼저 보고(`isDirectory`) 아니면 낱개 파일 자리로 풀었다 - 팩 구성에서는 대조할 것이 없었다.
             */
            [[nodiscard]] static expected<void> collectMigrationFiles( string_view folder, vector<AssetID>& outListFile )
            {
                if ( AssetIO::collectAssets( AssetID{ folder }, ".sql", outListFile ) == AssetFolderListing::Failed )
                {
                    StringBuilder<constant::buffer::kMax128> errorBuilder;
                    errorBuilder.appendFormat( "%#", folder );
                    return unexpected( Error{ ErrorKind::InvalidArgument, "cannot read the migration folder", errorBuilder.view() } );
                }
                return {};
            }

            /** @brief 폴더의 `.sql` 이 모두 목록의 마이그레이션(공통이나 방언 분기)인지 대조합니다. */
            [[nodiscard]] static expected<void> checkFolderMatchesList( string_view folder, const vector<string>& listStem )
            {
                vector<AssetID> listFilePath;
                if ( expected<void> result = collectMigrationFiles( folder, listFilePath ); result.has_value() == false )
                    return result;
                for ( const AssetID& filePath : listFilePath )
                {
                    const string  fileName = FileUtil::getFileNamePart( filePath.view() );
                    MigrationFile file;
                    if ( parseFileName( fileName, file ) == false )
                    {
                        StringBuilder<constant::buffer::kMax128> errorBuilder;
                        errorBuilder.appendFormat( "%#", fileName );
                        return unexpected( Error{ ErrorKind::InvalidArgument, "migration file is not named NNNN_name.sql or NNNN_name.<driver>.sql", errorBuilder.view() } );
                    }
                    StringBuilder<constant::buffer::kMax256> stemBuilder;
                    stemBuilder.appendFormat( "%#%#", string_view( fileName ).substr( 0, static_cast<size_t>( kVersionDigitCount + 1 ) ), file._name );
                    const string stem{ stemBuilder.view() };
                    const bool   bListed = VectorUtil::contains( listStem, stem );
                    if ( bListed == false )
                    {
                        StringBuilder<constant::buffer::kMax128> errorBuilder;
                        errorBuilder.appendFormat( "%#, %#, %#", fileName, SQLMigrationRunner::kListFileName, folder );
                        return unexpected( Error{ ErrorKind::InvalidArgument, "migration file is not listed in the migration list of its folder", errorBuilder.view() } );
                    }
                }
                return {};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<void> SQLMigrationRunner::loadMigrations( string_view folder, string_view driverName, vector<SQLMigration>& outListMigration )
    {
        vector<string> listStem;
        if ( expected<void> result = SQLMigrationRunnerInternal::readList( folder, listStem ); result.has_value() == false )
            return result;
        if ( expected<void> result = SQLMigrationRunnerInternal::checkFolderMatchesList( folder, listStem ); result.has_value() == false )
            return result;
        for ( size_t stemIndex = 0; stemIndex < listStem.size(); ++stemIndex )
        {
            const string&                             stem = listStem[stemIndex];
            SQLMigrationRunnerInternal::MigrationFile file;
            StringBuilder<constant::buffer::kMax256>  textBuilder3;
            textBuilder3.appendFormat( "%#.sql", stem );
            const bool bNamed = SQLMigrationRunnerInternal::parseFileName( string( textBuilder3.view() ), file ) && file._driverName.empty();
            if ( bNamed == false )
            {
                StringBuilder<constant::buffer::kMax128> errorBuilder;
                errorBuilder.appendFormat( "%#, %#", stem, SQLMigrationRunner::kListFileName );
                return unexpected( Error{ ErrorKind::InvalidArgument, "a migration in the migration list is not named NNNN_name", errorBuilder.view() } );
            }
            const int32 expectedVersion = static_cast<int32>( stemIndex ) + 1;
            if ( file._version != expectedVersion )
                return unexpected( SQLMigrationRunnerInternal::makeError( ErrorKind::InvalidArgument, "migration numbers must run 1, 2, 3 ... without gaps - expected this number at the stem", expectedVersion, stem ) );
            // 방언 분기 파일이 있으면 공통 파일 대신 쓴다. 다른 드라이버의 방언 파일은 읽지 않는다.
            StringBuilder<constant::buffer::kMax256> textBuilder2;
            textBuilder2.appendFormat( "%#.%#.sql", stem, driverName );
            const string                             driverPath = FileUtil::joinPath( folder, textBuilder2.view() );
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "%#.sql", stem );
            const string commonPath = FileUtil::joinPath( folder, textBuilder.view() );
            const bool   bHasDriver = AssetIO::hasAsset( AssetID{ driverPath } );
            if ( bHasDriver == false && AssetIO::hasAsset( AssetID{ commonPath } ) == false )
                return unexpected( SQLMigrationRunnerInternal::makeError( ErrorKind::NotFound, "the migration has no common file and no file for the driver", file._version, driverName ) );
            const string& path      = bHasDriver ? driverPath : commonPath;
            SQLMigration& migration = outListMigration.emplace_back();
            migration._version      = file._version;
            migration._name         = file._name;
            if ( AssetIO::readTextAsset( AssetID{ path }, migration._sql ) == false )
            {
                StringBuilder<constant::buffer::kMax128> errorBuilder;
                errorBuilder.appendFormat( "%#", path );
                return unexpected( Error{ ErrorKind::InvalidArgument, "cannot read migration file", errorBuilder.view() } );
            }
            SQLMigrationRunnerInternal::removeCarriageReturns( migration._sql );
        }
        return {};
    }

    expected<void> SQLMigrationRunner::collectDialectDriverNames( string_view folder, vector<string>& inoutListName )
    {
        vector<AssetID> listFilePath;
        if ( expected<void> result = SQLMigrationRunnerInternal::collectMigrationFiles( folder, listFilePath ); result.has_value() == false )
            return result;
        for ( const AssetID& filePath : listFilePath )
        {
            SQLMigrationRunnerInternal::MigrationFile file;
            if ( SQLMigrationRunnerInternal::parseFileName( FileUtil::getFileNamePart( filePath.view() ), file ) == false || file._driverName.empty() )
                continue; // 이름이 틀린 파일은 `loadMigrations` 가 오류로 멈춘다
            if ( VectorUtil::contains( inoutListName, file._driverName ) == false )
                inoutListName.push_back( file._driverName );
        }
        return {};
    }

    expected<void> SQLMigrationRunner::apply( ISQLConnection& connection, const vector<SQLMigration>& listMigration, UnixMilliseconds nowMs )
    {
        const SQLDialect& dialect = connection.getDialect();
        // PostgreSQL 의 `CREATE TABLE IF NOT EXISTS` 는 서버 둘이 동시에 돌면 진 쪽이 고유 키 위반(23505)을 받는다 - 다른 서버가 방금 만든 것이므로 있는 것이다(아래 락이 그 다음을 줄 세운다).
        const SQLResult createResult = connection.executeScript( SQLMigrationRunnerInternal::kCreateHistorySQL );
        if ( createResult != SQLResult::Ok && createResult != SQLResult::Constraint )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "cannot create the migration history table", connection.getLastErrorText() } );
        }
        if ( connection.executeScript( dialect._pBeginWrite ) != SQLResult::Ok )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "cannot begin the migration transaction", connection.getLastErrorText() } );
        }
        if ( connection.executeScript( dialect._pLockMigrationTable ) != SQLResult::Ok )
        {
            const Error error{ ErrorKind::ExternalFailure, "cannot lock the migration history table", connection.getLastErrorText() }; // 되돌리기가 마지막 오류 글을 바꾸기 전에 복사한다
            SQLMigrationRunnerInternal::rollback( connection );
            return unexpected( error );
        }
        SQLRowSet rowSet;
        if ( connection.execute( SQLMigrationRunnerInternal::kReadHistorySQL, nullptr, 0, &rowSet ) != SQLResult::Ok )
        {
            const Error error{ ErrorKind::ExternalFailure, "cannot read the migration history table", connection.getLastErrorText() }; // 되돌리기가 마지막 오류 글을 바꾸기 전에 복사한다
            SQLMigrationRunnerInternal::rollback( connection );
            return unexpected( error );
        }
        map<int32, string> mapAppliedChecksum;
        for ( const vector<SQLValue>& row : rowSet._listRow )
        {
            mapAppliedChecksum[static_cast<int32>( row[0]._integer )] = string( row[1].getText() );
        }
        // 이 빌드가 모르는 번호가 적용돼 있으면 더 새 빌드가 올린 스키마다 - 예전에는 말없이 지나가 옛 서버가 새 스키마에 쓸 수 있었다(Flyway 의 validate 와 같다).
        for ( const auto& [appliedVersion, appliedChecksum] : mapAppliedChecksum )
        {
            (void)appliedChecksum;
            if ( VectorUtil::findBy<&SQLMigration::_version>( listMigration, appliedVersion ) == nullptr )
            {
                SQLMigrationRunnerInternal::rollback( connection );
                return unexpected( SQLMigrationRunnerInternal::makeError( ErrorKind::Unsupported, "the migration is applied but unknown to this build - the database is newer", appliedVersion, "" ) );
            }
        }

        for ( const SQLMigration& migration : listMigration )
        {
            const string checksum  = computeChecksum( migration._sql );
            const auto   appliedIt = mapAppliedChecksum.find( migration._version );
            if ( appliedIt != mapAppliedChecksum.end() )
            {
                if ( appliedIt->second == checksum )
                    continue;
                SQLMigrationRunnerInternal::rollback( connection );
                return unexpected( SQLMigrationRunnerInternal::makeError( ErrorKind::InvalidArgument, "the migration changed after it was applied - add a new migration instead of editing it",
                                                                          migration._version, migration._name ) );
            }
            if ( connection.executeScript( substituteTokens( migration._sql, dialect ) ) != SQLResult::Ok )
            {
                const Error error = SQLMigrationRunnerInternal::makeError( ErrorKind::ExternalFailure, "the migration failed", migration._version, connection.getLastErrorText() );
                SQLMigrationRunnerInternal::rollback( connection );
                return unexpected( error );
            }
            const SQLValue arrParam[] = { SQLValue::makeInt64( migration._version ), SQLValue::makeText( migration._name ), SQLValue::makeText( checksum ),
                                          SQLValue::makeInt64( nowMs.count() ) };
            if ( connection.execute( SQLMigrationRunnerInternal::kInsertHistorySQL, arrParam, 4, nullptr ) != SQLResult::Ok )
            {
                const Error error{ ErrorKind::ExternalFailure, "cannot record a migration", connection.getLastErrorText() }; // 되돌리기가 마지막 오류 글을 바꾸기 전에 복사한다
                SQLMigrationRunnerInternal::rollback( connection );
                return unexpected( error );
            }
        }
        if ( connection.executeScript( "COMMIT" ) != SQLResult::Ok )
        {
            const Error error{ ErrorKind::ExternalFailure, "cannot commit the migrations", connection.getLastErrorText() }; // 되돌리기가 마지막 오류 글을 바꾸기 전에 복사한다
            SQLMigrationRunnerInternal::rollback( connection );
            return unexpected( error );
        }
        return {};
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
