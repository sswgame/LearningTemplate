/**
 * @file SqlDriverContract.h
 * @brief SQL 드라이버 계약 시험 — SQLite · PostgreSQL 드라이버가 같은 케이스를 같은 결과로 통과해야 합니다(풀 없이 시험 스레드에서 연결을 직접 쓴다).
 * @details 스위트 파일이 픽스처를 주고 `SW_SQL_DRIVER_CONTRACT_SUITE( 스위트, 픽스처 )` 를 부른다. 픽스처는
 *          `bool isReady()` · `ISqlConnection& getConnection()` · `string makeTableName( const utf8* )` 를 준다. 케이스마다 새 DB(또는 새 스키마)다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Storage/SqlStore/Sql/SqlDriver.h"
#include "GameFramework/Kits/Storage/SqlStore/SqlMigrationRunner.h"

#include "TestFramework/TestFramework.h"

namespace test
{
    /** @brief 드라이버 계약 케이스 몸들입니다. */
    struct SqlDriverContract
    {
        static sw::string makeSql( sw::ISqlConnection& connection, const sw::string& table, sw::string_view format )
        {
            sw::string sql = sw::StringUtil::replace( format, "{{table}}", table );
            return sw::SqlMigrationRunner::substituteTokens( sql, connection.getDialect() );
        }

        static sw::vector<uint8> makeBinaryBytes()
        {
            sw::vector<uint8> bytes;
            for ( int32 index = 0; index < 300; ++index )
            {
                bytes.push_back( static_cast<uint8>( index * 37 ) ); // 0x00 · 0xFF 모두 든다
            }
            return bytes;
        }

        template <typename FixtureType>
        static void runValuesRoundTrip( FixtureType& fixture )
        {
            using namespace sw;
            ISqlConnection& connection = fixture.getConnection();
            const string    table      = fixture.makeTableName( "values" );
            SW_ASSERT_TRUE_MSG( connection.executeScript( makeSql( connection, table, "CREATE TABLE {{table}} ( id BIGINT PRIMARY KEY, number BIGINT, text_value TEXT, "
                                                                                      "bytes {{blob}}, null_number BIGINT )" ) ) == SqlResult::Ok,
                                connection.getLastErrorText() );
            const vector<uint8> bytes     = makeBinaryBytes();
            const string        insertSql = makeSql( connection, table, "INSERT INTO {{table}} ( id, number, text_value, bytes, null_number ) VALUES ( ?, ?, ?, ?, ? )" );
            const SqlValue      arrMin[]  = { SqlValue::makeInt64( 1 ), SqlValue::makeInt64( INT64_MIN ), SqlValue::makeText( "" ), SqlValue::makeBlob( nullptr, 0 ), SqlValue{} };
            const SqlValue      arrMax[]  = { SqlValue::makeInt64( 2 ), SqlValue::makeInt64( INT64_MAX ), SqlValue::makeText( "글자 text" ),
                                              SqlValue::makeBlob( bytes.data(), static_cast<int32>( bytes.size() ) ), SqlValue{} };
            SW_ASSERT_TRUE_MSG( connection.execute( insertSql, arrMin, 5, nullptr ) == SqlResult::Ok, connection.getLastErrorText() );
            SW_ASSERT_TRUE_MSG( connection.execute( insertSql, arrMax, 5, nullptr ) == SqlResult::Ok, connection.getLastErrorText() );

            SqlRowSet rowSet;
            SW_ASSERT_TRUE( connection.execute( makeSql( connection, table, "SELECT number, text_value, bytes, null_number FROM {{table}} ORDER BY id" ), nullptr, 0,
                                                &rowSet ) == SqlResult::Ok );
            SW_ASSERT_EQUAL( size_t( 2 ), rowSet._listRow.size() );
            const vector<SqlValue>& first  = rowSet._listRow[0];
            const vector<SqlValue>& second = rowSet._listRow[1];
            SW_EXPECT_TRUE( first[0]._type == SqlValueType::Int64 && first[0]._integer == INT64_MIN );
            SW_EXPECT_TRUE( first[1]._type == SqlValueType::Text && first[1]._bytes.empty() ); // 빈 글은 NULL 이 아니다
            SW_EXPECT_TRUE( first[2]._type == SqlValueType::Blob && first[2]._bytes.empty() ); // 빈 바이트도
            SW_EXPECT_TRUE( first[3]._type == SqlValueType::Null );
            SW_EXPECT_TRUE( second[0]._integer == INT64_MAX );
            SW_EXPECT_TRUE( second[1].getText() == string_view( "글자 text" ) );
            SW_EXPECT_TRUE( second[2]._bytes == bytes );
        }

        template <typename FixtureType>
        static void runConstraintAndAffectedRows( FixtureType& fixture )
        {
            using namespace sw;
            ISqlConnection& connection = fixture.getConnection();
            const string    table      = fixture.makeTableName( "constraint" );
            SW_ASSERT_TRUE( connection.executeScript( makeSql( connection, table, "CREATE TABLE {{table}} ( id BIGINT PRIMARY KEY, number BIGINT )" ) ) == SqlResult::Ok );
            const string   insertSql   = makeSql( connection, table, "INSERT INTO {{table}} ( id, number ) VALUES ( ?, ? )" );
            const SqlValue arrFirst[]  = { SqlValue::makeInt64( 1 ), SqlValue::makeInt64( 10 ) };
            const SqlValue arrSecond[] = { SqlValue::makeInt64( 2 ), SqlValue::makeInt64( 10 ) };
            SW_ASSERT_TRUE( connection.execute( insertSql, arrFirst, 2, nullptr ) == SqlResult::Ok );
            SW_ASSERT_TRUE( connection.execute( insertSql, arrSecond, 2, nullptr ) == SqlResult::Ok );
            SW_EXPECT_TRUE( connection.execute( insertSql, arrFirst, 2, nullptr ) == SqlResult::Constraint );
            SW_EXPECT_TRUE( connection.getLastErrorText()[0] != '\0' );

            SqlRowSet      rowSet;
            const SqlValue arrUpdate[] = { SqlValue::makeInt64( 10 ) };
            SW_ASSERT_TRUE( connection.execute( makeSql( connection, table, "UPDATE {{table}} SET number = number + 1 WHERE number = ?" ), arrUpdate, 1, &rowSet ) ==
                            SqlResult::Ok );
            SW_EXPECT_EQUAL( int64( 2 ), rowSet._affectedRowCount );
            SW_ASSERT_TRUE( connection.execute( makeSql( connection, table, "UPDATE {{table}} SET number = number + 1 WHERE number = ?" ), arrUpdate, 1, &rowSet ) ==
                            SqlResult::Ok );
            SW_EXPECT_EQUAL( int64( 0 ), rowSet._affectedRowCount );
        }

        template <typename FixtureType>
        static void runStatementCacheKeepsNoBindings( FixtureType& fixture )
        {
            using namespace sw;
            ISqlConnection& connection = fixture.getConnection();
            const string    table      = fixture.makeTableName( "cache" );
            SW_ASSERT_TRUE( connection.executeScript( makeSql( connection, table, "CREATE TABLE {{table}} ( id BIGINT PRIMARY KEY, number BIGINT )" ) ) == SqlResult::Ok );
            const string   insertSql = makeSql( connection, table, "INSERT INTO {{table}} ( id, number ) VALUES ( ?, ? )" );
            const SqlValue arrFull[] = { SqlValue::makeInt64( 1 ), SqlValue::makeInt64( 5 ) };
            const SqlValue arrHalf[] = { SqlValue::makeInt64( 2 ) };
            SW_ASSERT_TRUE( connection.execute( insertSql, arrFull, 2, nullptr ) == SqlResult::Ok );
            SW_ASSERT_TRUE( connection.execute( insertSql, arrHalf, 1, nullptr ) == SqlResult::Ok ); // 같은 준비문 — 빠진 매개변수는 NULL
            SqlRowSet rowSet;
            SW_ASSERT_TRUE( connection.execute( makeSql( connection, table, "SELECT number FROM {{table}} WHERE id = 2" ), nullptr, 0, &rowSet ) == SqlResult::Ok );
            SW_ASSERT_EQUAL( size_t( 1 ), rowSet._listRow.size() );
            SW_EXPECT_TRUE( rowSet._listRow[0][0]._type == SqlValueType::Null );
        }

        template <typename FixtureType>
        static void runScriptAndRollback( FixtureType& fixture )
        {
            using namespace sw;
            ISqlConnection& connection = fixture.getConnection();
            const string    table      = fixture.makeTableName( "script" );
            SW_ASSERT_TRUE( connection.executeScript( makeSql( connection, table, "CREATE TABLE {{table}} ( id BIGINT PRIMARY KEY ); "
                                                                                  "INSERT INTO {{table}} ( id ) VALUES ( 1 ); INSERT INTO {{table}} ( id ) VALUES ( 2 );" ) ) ==
                            SqlResult::Ok );
            SW_ASSERT_TRUE( connection.executeScript( connection.getDialect()._pBeginWrite ) == SqlResult::Ok );
            SW_ASSERT_TRUE( connection.executeScript( makeSql( connection, table, "INSERT INTO {{table}} ( id ) VALUES ( 3 )" ) ) == SqlResult::Ok );
            SW_ASSERT_TRUE( connection.executeScript( "ROLLBACK" ) == SqlResult::Ok );
            SqlRowSet rowSet;
            SW_ASSERT_TRUE( connection.execute( makeSql( connection, table, "SELECT id FROM {{table}} ORDER BY id" ), nullptr, 0, &rowSet ) == SqlResult::Ok );
            SW_EXPECT_EQUAL( size_t( 2 ), rowSet._listRow.size() );
            SW_EXPECT_TRUE( connection.executeScript( "THIS IS NOT SQL" ) == SqlResult::Error );
            SW_EXPECT_TRUE( connection.isAlive() ); // 문법 오류는 연결을 죽이지 않는다
        }

        static void writeMigration( const sw::string& folder, sw::string_view fileName, sw::string_view sql )
        {
            SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, fileName ), sql ) );
        }

        static bool hasTable( sw::ISqlConnection& connection, sw::string_view table )
        {
            sw::SqlRowSet    rowSet;
            const sw::string sql = sw::string( "SELECT COUNT(*) FROM " ) + sw::string( table );
            return connection.execute( sql, nullptr, 0, &rowSet ) == sw::SqlResult::Ok;
        }

        template <typename FixtureType>
        static void runMigrations( FixtureType& fixture )
        {
            using namespace sw;
            ISqlConnection& connection = fixture.getConnection();
            const string    driverName = connection.getDialect()._pDriverName;
            const string    folder     = ::test::makeTempDirectory( "migrations" );
            const string    first      = fixture.makeTableName( "first" );
            const string    second     = fixture.makeTableName( "second" );
            writeMigration( folder, "0001_first.sql", "CREATE TABLE " + first + " ( id BIGINT PRIMARY KEY, bytes {{blob}} );\r\n" );
            writeMigration( folder, "0002_second.sql", "CREATE TABLE common_only_must_not_run ( id BIGINT );" );
            writeMigration( folder, "0002_second." + driverName + ".sql", "CREATE TABLE " + second + " ( id BIGINT );" ); // 방언 갈래가 공통 파일을 대신한다
            writeMigration( folder, "0002_second.otherdriver.sql", "THIS IS NOT SQL" );                                   // 다른 드라이버 갈래는 읽지 않는다

            vector<SqlMigration> listMigration;
            string               error;
            SW_ASSERT_TRUE_MSG( SqlMigrationRunner::loadMigrations( folder, driverName, listMigration, error ), error.c_str() );
            SW_ASSERT_EQUAL( size_t( 2 ), listMigration.size() );
            SW_EXPECT_TRUE( listMigration[0]._sql.find( '\r' ) == string::npos ); // 줄 끝을 맞춘다 — 체크섬이 체크아웃마다 같다
            SW_ASSERT_TRUE_MSG( SqlMigrationRunner::apply( connection, listMigration, 100, error ), error.c_str() );
            SW_EXPECT_TRUE( hasTable( connection, first ) );
            SW_EXPECT_TRUE( hasTable( connection, second ) );
            SW_EXPECT_FALSE( hasTable( connection, "common_only_must_not_run" ) );
            SW_ASSERT_TRUE_MSG( SqlMigrationRunner::apply( connection, listMigration, 200, error ), error.c_str() ); // 두 번째는 아무것도 안 한다

            // 세 번째가 깨졌다 — 넷째도 적용되지 않는다(한 트랜잭션).
            const string third  = fixture.makeTableName( "third" );
            const string fourth = fixture.makeTableName( "fourth" );
            writeMigration( folder, "0003_third.sql", "CREATE TABLE " + third + " ( id BIGINT );" );
            writeMigration( folder, "0004_broken.sql", "CREATE TABLE " + fourth + " ( id BIGINT ); THIS IS NOT SQL;" );
            listMigration.clear();
            SW_ASSERT_TRUE( SqlMigrationRunner::loadMigrations( folder, driverName, listMigration, error ) );
            SW_EXPECT_FALSE( SqlMigrationRunner::apply( connection, listMigration, 300, error ) );
            SW_EXPECT_FALSE( hasTable( connection, third ) );
            SW_EXPECT_FALSE( hasTable( connection, fourth ) );

            // 적용된 파일을 고치면 거절한다.
            SW_ASSERT_TRUE( FileUtil::tryRemoveFile( FileUtil::joinPath( folder, "0004_broken.sql" ) ) );
            writeMigration( folder, "0001_first.sql", "CREATE TABLE " + first + " ( id BIGINT PRIMARY KEY, bytes {{blob}}, extra BIGINT );" );
            listMigration.clear();
            SW_ASSERT_TRUE( SqlMigrationRunner::loadMigrations( folder, driverName, listMigration, error ) );
            error.clear();
            SW_EXPECT_FALSE( SqlMigrationRunner::apply( connection, listMigration, 400, error ) );
            SW_EXPECT_TRUE( error.find( "changed after it was applied" ) != string::npos );
            SW_EXPECT_FALSE( hasTable( connection, third ) );

            // 번호에 빈틈이 있으면 읽지 않는다.
            writeMigration( folder, "0006_gap.sql", "CREATE TABLE gap ( id BIGINT );" );
            listMigration.clear();
            SW_EXPECT_FALSE( SqlMigrationRunner::loadMigrations( folder, driverName, listMigration, error ) );
        }
    };
} // namespace test

#define SW_SQL_DRIVER_CONTRACT_CASE( SuiteName, FixtureType, CaseName, RunFunction ) \
    SW_TEST_CASE( SuiteName, CaseName )                                              \
    {                                                                                \
        FixtureType fixture;                                                         \
        SW_ASSERT_TRUE( fixture.isReady() );                                         \
        test::SqlDriverContract::RunFunction( fixture );                             \
    }

/** @brief 드라이버 계약 케이스 다섯을 @p SuiteName 스위트로 만듭니다. */
#define SW_SQL_DRIVER_CONTRACT_SUITE( SuiteName, FixtureType )                                                                \
    SW_SQL_DRIVER_CONTRACT_CASE( SuiteName, FixtureType, ValuesRoundTripIncludingEmptyAndBinary, runValuesRoundTrip )         \
    SW_SQL_DRIVER_CONTRACT_CASE( SuiteName, FixtureType, ConstraintAndAffectedRowsAreReported, runConstraintAndAffectedRows ) \
    SW_SQL_DRIVER_CONTRACT_CASE( SuiteName, FixtureType, StatementCacheKeepsNoBindings, runStatementCacheKeepsNoBindings )    \
    SW_SQL_DRIVER_CONTRACT_CASE( SuiteName, FixtureType, ScriptRunsSeveralStatementsAndRollbackUndoes, runScriptAndRollback ) \
    SW_SQL_DRIVER_CONTRACT_CASE( SuiteName, FixtureType, MigrationsApplyOnceAtomicallyAndRejectEdits, runMigrations )
