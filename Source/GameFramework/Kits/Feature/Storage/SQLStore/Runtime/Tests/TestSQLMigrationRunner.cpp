#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/TimePoint.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQLMigrationRunner.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/Tests/FakeSQLDriver.h"

// SQLMigrationRunnerTest — 마이그레이션: 목록 읽기 규칙(번호, 방언 분기, 목록에 없는 파일), 토큰 치환, 체크섬, 적용 순서와 실패 때의 되돌리기(가짜 드라이버), 실제 SQLite 의 원자성(nogpu).

using namespace sw;

namespace
{
    /** @brief 케이스 폴더를 리소스 폴더 `testproject/sql` 로 붙이고 파일을 씁니다. */
    struct MigrationFolderFixture
    {
        test::ScopedScratchDomain _domain{};
        string                    _diskFolder{};
        string                    _folderID{ string( test::ScopedScratchDomain::kDomain ) + "/sql" };

        MigrationFolderFixture()
            : _diskFolder{ _domain.makeDiskFolder( "sql" ) }
        {
        }

        void write( string_view fileName, string_view text ) { SW_EXPECT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( _diskFolder, fileName ), text ) ); }

        /** @brief `<Migration _name="…" />` 를 이름마다 적은 목록 파일을 씁니다. */
        void writeList( std::initializer_list<const utf8*> listName )
        {
            string text = "<SQLMigrationList>";
            for ( const utf8* pName : listName )
            {
                text += string( "<Migration _name=\"" ) + pName + "\" />";
            }
            text += "</SQLMigrationList>";
            write( SQLMigrationRunner::kListFileName, text );
        }

        bool load( string_view driverName, vector<SQLMigration>& outListMigration, string& outError ) const
        {
            return SQLMigrationRunner::loadMigrations( _folderID, driverName, outListMigration, outError );
        }
    };

    struct SQLMigrationRunnerTestUtil
    {
        static vector<SQLMigration> makeTwoMigrations()
        {
            vector<SQLMigration> listMigration;
            SQLMigration&        first = listMigration.emplace_back();
            first._version             = 1;
            first._name                = "first";
            first._sql                 = "CREATE FIRST {{blob}}";
            SQLMigration& second       = listMigration.emplace_back();
            second._version            = 2;
            second._name               = "second";
            second._sql                = "CREATE SECOND";
            return listMigration;
        }

        static vector<SQLValue> makeHistoryRow( int32 version, string_view sql ) { return vector<SQLValue>{ SQLValue::makeInt64( version ), SQLValue::makeText( SQLMigrationRunner::computeChecksum( sql ) ) }; }

        /** @brief @p driver 의 연결로 두 마이그레이션을 적용합니다. */
        static bool applyTwo( test::FakeSQLDriver& driver, string& outError )
        {
            unique_ptr<ISQLConnection> connection = driver.openConnection( "", "", outError );
            return SQLMigrationRunner::apply( *connection, makeTwoMigrations(), UnixMilliseconds{ 1234 }, outError );
        }

        static unique_ptr<ISQLConnection> openSQLite()
        {
            string      error;
            ISQLDriver* pDriver = SQLDriverRegistry::findDriver( "sqlite", error );
            return pDriver != nullptr ? pDriver->openConnection( ":memory:", "", error ) : nullptr;
        }
    };
} // namespace

SW_TEST_CASE( SQLMigrationRunnerTest, TokensBecomeTheDialectsTypes )
{
    const SQLDialect& dialect = test::FakeSQLDriver::getDialect();
    SW_EXPECT_EQUAL( string( "a BLOBTYPE KEYTEXT AUTOID BLOBTYPE {{other}}" ), SQLMigrationRunner::substituteTokens( "a {{blob}} {{keytext}} {{autoid}} {{blob}} {{other}}", dialect ) );
}

SW_TEST_CASE( SQLMigrationRunnerTest, ChecksumSeesEveryByte )
{
    SW_EXPECT_EQUAL( SQLMigrationRunner::computeChecksum( "CREATE" ), SQLMigrationRunner::computeChecksum( "CREATE" ) );
    SW_EXPECT_TRUE( SQLMigrationRunner::computeChecksum( "CREATE" ) != SQLMigrationRunner::computeChecksum( "create" ) ); // 대소문자를 가린다
    SW_EXPECT_TRUE( SQLMigrationRunner::computeChecksum( "CREATE" ) != SQLMigrationRunner::computeChecksum( "CREATE " ) );
}

// ---------------------------------------------------------------------------------------------------------------------
// 적용 — 가짜 드라이버로 문장 순서와 실패 처리를 본다
// ---------------------------------------------------------------------------------------------------------------------

SW_TEST_CASE( SQLMigrationRunnerTest, AppliesEverythingInOneTransaction )
{
    test::FakeSQLDriver driver;
    string              error;
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunnerTestUtil::applyTwo( driver, error ), error );
    const vector<string> listSQL = driver.getSQLLog();
    SW_REQUIRE_EQUAL( size_t( 9 ), listSQL.size() );
    SW_EXPECT_TRUE( listSQL[0].find( "CREATE TABLE IF NOT EXISTS sw_schema_migration" ) != string::npos );
    SW_EXPECT_EQUAL( string( "BEGIN WRITE" ), listSQL[1] );
    SW_EXPECT_EQUAL( string( "LOCK HISTORY" ), listSQL[2] );
    SW_EXPECT_TRUE( listSQL[3].find( "SELECT version, checksum" ) != string::npos );
    SW_EXPECT_EQUAL( string( "CREATE FIRST BLOBTYPE" ), listSQL[4] ); // 토큰을 치환한 글이 돈다
    SW_EXPECT_TRUE( listSQL[5].find( "INSERT INTO sw_schema_migration" ) != string::npos );
    SW_EXPECT_EQUAL( string( "CREATE SECOND" ), listSQL[6] );
    SW_EXPECT_TRUE( listSQL[7].find( "INSERT INTO sw_schema_migration" ) != string::npos );
    SW_EXPECT_EQUAL( string( "COMMIT" ), listSQL[8] );
}

/** @brief 기록은 치환 전 원문의 체크섬이다 — 드라이버가 달라도 같은 파일이면 같은 값이고, 값은 묶은 매개변수로만 간다. */
SW_TEST_CASE( SQLMigrationRunnerTest, HistoryRecordsTheOriginalChecksumAsParameters )
{
    test::FakeSQLDriver driver;
    string              error;
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunnerTestUtil::applyTwo( driver, error ), error );
    const vector<test::FakeSQLDriver::Statement> listStatement = driver.getStatements();
    SW_REQUIRE_TRUE( listStatement.size() > 5 );
    const vector<SQLValue>& listParam = listStatement[5]._listParam;
    SW_REQUIRE_EQUAL( size_t( 4 ), listParam.size() );
    SW_EXPECT_EQUAL( int64( 1 ), listParam[0]._integer );
    SW_EXPECT_TRUE( listParam[1].getText() == "first" );
    SW_EXPECT_TRUE( listParam[2].getText() == SQLMigrationRunner::computeChecksum( "CREATE FIRST {{blob}}" ) );
    SW_EXPECT_EQUAL( int64( 1234 ), listParam[3]._integer );
}

SW_TEST_CASE( SQLMigrationRunnerTest, AppliedMigrationIsSkipped )
{
    test::FakeSQLDriver driver;
    driver.addRows( "SELECT version, checksum", { SQLMigrationRunnerTestUtil::makeHistoryRow( 1, "CREATE FIRST {{blob}}" ) } );
    string error;
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunnerTestUtil::applyTwo( driver, error ), error );
    SW_EXPECT_EQUAL( 0, driver.countSQL( "CREATE FIRST" ) );
    SW_EXPECT_EQUAL( 1, driver.countSQL( "CREATE SECOND" ) );
    SW_EXPECT_EQUAL( 1, driver.countSQL( "COMMIT" ) );
}

SW_TEST_CASE( SQLMigrationRunnerTest, EditedAppliedMigrationIsRefused )
{
    test::FakeSQLDriver driver;
    driver.addRows( "SELECT version, checksum", { SQLMigrationRunnerTestUtil::makeHistoryRow( 1, "the file used to say this" ) } );
    string error;
    SW_EXPECT_FALSE( SQLMigrationRunnerTestUtil::applyTwo( driver, error ) );
    SW_EXPECT_TRUE( error.find( "changed after it was applied" ) != string::npos );
    SW_EXPECT_EQUAL( 1, driver.countSQL( "ROLLBACK" ) );
    SW_EXPECT_EQUAL( 0, driver.countSQL( "COMMIT" ) );
}

/**
 * @brief 이 빌드가 모르는 번호가 적용돼 있으면 거절한다.
 * @details 결함: 적용 기록 쪽의 번호를 보지 않아, 새 빌드가 스키마를 올린 DB 에 옛 서버가 말없이 붙어 썼다.
 */
SW_TEST_CASE( SQLMigrationRunnerTest, NewerDatabaseIsRefused )
{
    test::FakeSQLDriver driver;
    driver.addRows( "SELECT version, checksum", { SQLMigrationRunnerTestUtil::makeHistoryRow( 1, "CREATE FIRST {{blob}}" ), SQLMigrationRunnerTestUtil::makeHistoryRow( 2, "CREATE SECOND" ),
                                                  SQLMigrationRunnerTestUtil::makeHistoryRow( 3, "from a newer build" ) } );
    string error;
    SW_EXPECT_FALSE( SQLMigrationRunnerTestUtil::applyTwo( driver, error ) );
    SW_EXPECT_TRUE( error.find( "newer" ) != string::npos );
    SW_EXPECT_EQUAL( 1, driver.countSQL( "ROLLBACK" ) );
    SW_EXPECT_EQUAL( 0, driver.countSQL( "COMMIT" ) );
}

/**
 * @brief 기록 표를 만들다 고유 키 위반을 받으면 다른 서버가 방금 만든 것이다 — 계속한다.
 * @details 결함: PostgreSQL 의 `CREATE TABLE IF NOT EXISTS` 는 동시에 돌면 진 쪽이 23505 를 받는다. 서버 여럿이 함께 뜰 때 하나가 기동에 실패했다. 머리말의 "서버 여러 대가 동시에 시작해도" 와 어긋났다.
 */
SW_TEST_CASE( SQLMigrationRunnerTest, LostHistoryTableRaceIsNotAFailure )
{
    test::FakeSQLDriver driver;
    driver.addResult( "CREATE TABLE IF NOT EXISTS", SQLResult::Constraint );
    string error;
    SW_EXPECT_TRUE( SQLMigrationRunnerTestUtil::applyTwo( driver, error ) );
    SW_EXPECT_EQUAL( 1, driver.countSQL( "COMMIT" ) );
}

/** @brief 트랜잭션을 열기 전의 실패는 되돌릴 것이 없다. 연 뒤의 실패는 모두 되돌리고 커밋하지 않는다. */
SW_TEST_CASE( SQLMigrationRunnerTest, FailuresRollBackOnlyWhatWasOpened )
{
    struct FailureCase
    {
        const utf8* _pFragment;
        int32       _expectedRollbackCount;
        int32       _expectedCommitCount;
    };
    const FailureCase arrCase[] = {
        {"CREATE TABLE IF NOT EXISTS", 0, 0},
        {               "BEGIN WRITE", 0, 0},
        {              "LOCK HISTORY", 1, 0},
        {  "SELECT version, checksum", 1, 0},
        {             "CREATE SECOND", 1, 0},
        {     "INSERT INTO sw_schema", 1, 0},
        {                    "COMMIT", 1, 1},
    };
    for ( const FailureCase& failure : arrCase )
    {
        test::FakeSQLDriver driver;
        driver.addResult( failure._pFragment, SQLResult::Error );
        string error;
        SW_EXPECT_FALSE( SQLMigrationRunnerTestUtil::applyTwo( driver, error ) );
        SW_EXPECT_TRUE_MSG( error.empty() == false, failure._pFragment );
        SW_EXPECT_EQUAL( failure._expectedRollbackCount, driver.countSQL( "ROLLBACK" ) );
        SW_EXPECT_EQUAL( failure._expectedCommitCount, driver.countSQL( "COMMIT" ) );
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// 실제 SQLite — 되돌리기가 정말 되돌린다
// ---------------------------------------------------------------------------------------------------------------------

SW_TEST_CASE( SQLMigrationRunnerTest, FailedRunLeavesTheSQLiteDatabaseUntouched )
{
    unique_ptr<ISQLConnection> connection = SQLMigrationRunnerTestUtil::openSQLite();
    SW_REQUIRE_NOT_NULL( connection );
    vector<SQLMigration> listMigration;
    SQLMigration&        good = listMigration.emplace_back();
    good._version             = 1;
    good._name                = "good";
    good._sql                 = "CREATE TABLE made_by_good ( x {{blob}} );";
    SQLMigration& bad         = listMigration.emplace_back();
    bad._version              = 2;
    bad._name                 = "bad";
    bad._sql                  = "THIS IS NOT SQL;";

    string error;
    SW_EXPECT_FALSE( SQLMigrationRunner::apply( *connection, listMigration, UnixMilliseconds{ 1 }, error ) );
    SW_EXPECT_TRUE( error.empty() == false );
    SQLRowSet rowSet;
    SW_REQUIRE_TRUE( connection->execute( "SELECT count( * ) FROM sqlite_master WHERE name = 'made_by_good'", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), rowSet._listRow[0][0]._integer );
    SW_REQUIRE_TRUE( connection->execute( "SELECT count( * ) FROM sw_schema_migration", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), rowSet._listRow[0][0]._integer );

    listMigration.pop_back();
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunner::apply( *connection, listMigration, UnixMilliseconds{ 1 }, error ), error ); // 트랜잭션을 남기지 않았으니 다시 된다
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunner::apply( *connection, listMigration, UnixMilliseconds{ 2 }, error ), error ); // 두 번째는 할 일이 없다
    SW_REQUIRE_TRUE( connection->execute( "SELECT count( * ) FROM sw_schema_migration", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_EQUAL( int64( 1 ), rowSet._listRow[0][0]._integer );

    listMigration[0]._sql += " -- edited";
    SW_EXPECT_FALSE( SQLMigrationRunner::apply( *connection, listMigration, UnixMilliseconds{ 3 }, error ) );
}

// ---------------------------------------------------------------------------------------------------------------------
// 목록 읽기
// ---------------------------------------------------------------------------------------------------------------------

/** @brief 저장소의 실제 폴더가 두 드라이버 모두에서 읽힌다. 방언 분기는 그 드라이버 파일을 쓴다. */
SW_TEST_CASE( SQLMigrationRunnerTest, ShippedFoldersLoad )
{
    string               error;
    vector<SQLMigration> listSQLite;
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunner::loadMigrations( "common/sql/servicestore", "sqlite", listSQLite, error ), error );
    vector<SQLMigration> listPostgres;
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunner::loadMigrations( "common/sql/servicestore", "postgres", listPostgres, error ), error );
    SW_REQUIRE_EQUAL( size_t( 2 ), listSQLite.size() );
    SW_REQUIRE_EQUAL( size_t( 2 ), listPostgres.size() );
    SW_EXPECT_EQUAL( listSQLite[0]._sql, listPostgres[0]._sql );  // 공통 파일
    SW_EXPECT_TRUE( listSQLite[1]._sql != listPostgres[1]._sql ); // 방언 분기

    vector<SQLMigration> listLocal;
    SW_EXPECT_TRUE( SQLMigrationRunner::loadMigrations( "common/sql/localstore", "sqlite", listLocal, error ) );
    vector<string> listDriverName;
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunner::collectDialectDriverNames( "common/sql/servicestore", listDriverName, error ), error );
    SW_EXPECT_EQUAL( size_t( 2 ), listDriverName.size() );
}

SW_TEST_CASE( SQLMigrationRunnerTest, ListPicksDialectFileAndNormalizesLineEnds )
{
    MigrationFolderFixture fixture;
    fixture.writeList( { "0001_a", "0002_b" } );
    fixture.write( "0001_a.sql", "COMMON\r\nA" );
    fixture.write( "0001_a.alpha.sql", "ALPHA A" );
    fixture.write( "0002_b.sql", "COMMON B" );

    string               error;
    vector<SQLMigration> listAlpha;
    SW_REQUIRE_TRUE_MSG( fixture.load( "alpha", listAlpha, error ), error );
    SW_REQUIRE_EQUAL( size_t( 2 ), listAlpha.size() );
    SW_EXPECT_EQUAL( string( "ALPHA A" ), listAlpha[0]._sql );
    SW_EXPECT_EQUAL( 1, listAlpha[0]._version );
    SW_EXPECT_EQUAL( string( "a" ), listAlpha[0]._name );
    SW_EXPECT_EQUAL( string( "COMMON B" ), listAlpha[1]._sql );

    vector<SQLMigration> listBeta;
    SW_REQUIRE_TRUE_MSG( fixture.load( "beta", listBeta, error ), error );
    SW_REQUIRE_EQUAL( size_t( 2 ), listBeta.size() );
    SW_EXPECT_EQUAL( string( "COMMON\nA" ), listBeta[0]._sql ); // 체크아웃의 줄 끝이 체크섬을 바꾸지 않는다
}

SW_TEST_CASE( SQLMigrationRunnerTest, ListRejectsBadShapes )
{
    MigrationFolderFixture fixture;
    fixture.write( "0001_a.sql", "A" );
    string               error;
    vector<SQLMigration> listMigration;

    fixture.write( SQLMigrationRunner::kListFileName, "<Other />" );
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    fixture.write( SQLMigrationRunner::kListFileName, "<SQLMigrationList _extra=\"1\"><Migration _name=\"0001_a\" /></SQLMigrationList>" );
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    fixture.write( SQLMigrationRunner::kListFileName, "<SQLMigrationList></SQLMigrationList>" );
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    fixture.write( SQLMigrationRunner::kListFileName, "<SQLMigrationList><Other _name=\"0001_a\" /></SQLMigrationList>" );
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    fixture.write( SQLMigrationRunner::kListFileName, "<SQLMigrationList><Migration _name=\"0001_a\" _more=\"x\" /></SQLMigrationList>" );
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    fixture.writeList( { "first" } ); // 번호가 없는 이름
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    fixture.writeList( { "0001_a.alpha" } ); // 목록에는 방언을 적지 않는다
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
}

SW_TEST_CASE( SQLMigrationRunnerTest, NumbersMustRunWithoutGaps )
{
    MigrationFolderFixture fixture;
    fixture.writeList( { "0001_a", "0003_c" } );
    fixture.write( "0001_a.sql", "A" );
    fixture.write( "0003_c.sql", "C" );
    string               error;
    vector<SQLMigration> listMigration;
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    SW_EXPECT_TRUE( error.find( "without gaps" ) != string::npos );
}

/** @brief 목록에 빠뜨린 파일은 조용히 건너뛰지 않는다. */
SW_TEST_CASE( SQLMigrationRunnerTest, FileMissingFromTheListIsRefused )
{
    MigrationFolderFixture fixture;
    fixture.writeList( { "0001_a" } );
    fixture.write( "0001_a.sql", "A" );
    fixture.write( "0002_forgotten.sql", "B" );
    string               error;
    vector<SQLMigration> listMigration;
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    SW_EXPECT_TRUE( error.find( "0002_forgotten" ) != string::npos );
}

/** @brief 이름이 틀린 파일도 건너뛰지 않는다. */
SW_TEST_CASE( SQLMigrationRunnerTest, BadlyNamedFileIsRefused )
{
    MigrationFolderFixture fixture;
    fixture.writeList( { "0001_a" } );
    fixture.write( "0001_a.sql", "A" );
    fixture.write( "notes.sql", "B" );
    string               error;
    vector<SQLMigration> listMigration;
    SW_EXPECT_FALSE( fixture.load( "alpha", listMigration, error ) );
    SW_EXPECT_TRUE( error.find( "notes.sql" ) != string::npos );
}

SW_TEST_CASE( SQLMigrationRunnerTest, DriverWithoutItsFileIsRefusedAndNamesAreCollected )
{
    MigrationFolderFixture fixture;
    fixture.writeList( { "0001_a" } );
    fixture.write( "0001_a.alpha.sql", "ALPHA" );
    fixture.write( "0001_a.beta.sql", "BETA" );
    string               error;
    vector<SQLMigration> listMigration;
    SW_EXPECT_FALSE( fixture.load( "gamma", listMigration, error ) ); // 공통 파일도 없다 — 다른 방언으로 대신하지 않는다
    SW_EXPECT_TRUE( error.find( "gamma" ) != string::npos );

    vector<string> listDriverName{ "alpha" };
    SW_REQUIRE_TRUE_MSG( SQLMigrationRunner::collectDialectDriverNames( fixture._folderID, listDriverName, error ), error );
    SW_EXPECT_EQUAL( size_t( 2 ), listDriverName.size() ); // 겹치지 않게 더한다
}
