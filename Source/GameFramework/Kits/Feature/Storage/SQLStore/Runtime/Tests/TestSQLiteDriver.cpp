#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/Tests/FakeSQLDriver.h"

#include <algorithm>
#include <limits>

namespace
{
    /** @brief 실패의 문장과 대상을 이은 글입니다(성공이면 빈 글). 오류 글을 확인하는 테스트가 씁니다. */
    template <typename T>
    sw::string describeError( const sw::expected<T>& result )
    {
        return result.has_value() ? sw::string{} : sw::string( result.error().message() ) + " " + sw::string( result.error().detail() );
    }
} // namespace

// SQLiteDriverTest · SQLDriverRegistryTest — 드라이버 계약을 실제 SQLite(메모리 DB)로 본다: 값 왕복, 매개변수가 문장이 되지 않음, 한 문 규칙, 자리 수,
// 제약 종류, 바뀐 행 수, 실패 뒤 문장 재사용, 여는 실패. 등록표는 이름 찾기와 범위 등록(nogpu, 외부 DB 없음).

using namespace sw;

namespace
{
    struct SQLiteDriverTestUtil
    {
        static ISQLDriver* findDriver() { return SQLDriverRegistry::findDriver( "sqlite" ).value_or( nullptr ); }

        /** @brief 메모리 DB 를 열고 표 `t( a, b )` 를 만듭니다. */
        static unique_ptr<ISQLConnection> openWithTable()
        {
            ISQLDriver* pDriver = findDriver();
            if ( pDriver == nullptr )
                return nullptr;
            unique_ptr<ISQLConnection> connection = pDriver->openConnection( ":memory:", "" ).value_or( nullptr );
            if ( connection != nullptr && connection->executeScript( "CREATE TABLE t ( a TEXT PRIMARY KEY, b INTEGER NOT NULL )" ) != SQLResult::Ok )
                return nullptr;
            return connection;
        }
    };
} // namespace

SW_TEST_CASE( SQLDriverRegistryTest, UnknownNameExplainsWhatIsAvailable )
{
    string error;
    error = describeError( SQLDriverRegistry::findDriver( "no_such_driver" ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "no_such_driver" ) != string::npos );
    SW_EXPECT_TRUE( error.find( "sqlite" ) != string::npos ); // 있는 드라이버를 알려 준다 — 다른 드라이버로 대신하지 않는다
}

SW_TEST_CASE( SQLDriverRegistryTest, RegistrationLivesOnlyInItsScope )
{
    test::FakeSQLDriver driver;
    {
        const SQLDriverRegistration registration{ driver };
        SW_EXPECT_TRUE( SQLDriverRegistry::findDriver( test::FakeSQLDriver::kName ).value_or( nullptr ) == &driver );
        vector<string> listName;
        SQLDriverRegistry::getDriverNames( listName );
        SW_EXPECT_TRUE( std::find( listName.begin(), listName.end(), string( test::FakeSQLDriver::kName ) ) != listName.end() );
    }
    SW_EXPECT_FALSE( SQLDriverRegistry::findDriver( test::FakeSQLDriver::kName ) );
}

SW_TEST_CASE( SQLiteDriverTest, ValuesRoundTripWithTheirKinds )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    const uint8    arrByte[]  = { 0x00, 0xFF, 0x00, 0x7F };
    const SQLValue arrParam[] = { SQLValue::makeInt64( std::numeric_limits<int64>::min() ), SQLValue::makeInt64( std::numeric_limits<int64>::max() ), SQLValue::makeText( "한글 text" ),
                                  SQLValue::makeBlob( arrByte, 4 ), SQLValue{} };
    SQLRowSet      rowSet;
    SW_REQUIRE_TRUE( connection->execute( "SELECT ?, ?, ?, ?, ?", arrParam, 5, &rowSet ) == SQLResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rowSet._listRow.size() );
    const vector<SQLValue>& row = rowSet._listRow[0];
    SW_REQUIRE_EQUAL( size_t( 5 ), row.size() );
    SW_EXPECT_TRUE( row[0]._type == SQLValueType::Int64 && row[0]._integer == std::numeric_limits<int64>::min() );
    SW_EXPECT_TRUE( row[1]._type == SQLValueType::Int64 && row[1]._integer == std::numeric_limits<int64>::max() );
    SW_EXPECT_TRUE( row[2]._type == SQLValueType::Text && row[2].getText() == "한글 text" );
    SW_EXPECT_TRUE( row[3]._type == SQLValueType::Blob && row[3]._bytes == vector<uint8>( arrByte, arrByte + 4 ) ); // 중간의 0 이 글 끝이 아니다
    SW_EXPECT_TRUE( row[4]._type == SQLValueType::Null );
}

/** @brief 빈 문자열과 빈 바이트는 NULL 이 아니다 — 데이터 포인터가 비면 sqlite3 가 NULL 로 묶는 함정을 막았다. */
SW_TEST_CASE( SQLiteDriverTest, EmptyTextAndBlobAreNotNull )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    const SQLValue arrParam[] = { SQLValue::makeText( "" ), SQLValue::makeBlob( nullptr, 0 ) };
    SQLRowSet      rowSet;
    SW_REQUIRE_TRUE( connection->execute( "SELECT ?1 IS NULL, ?2 IS NULL, typeof( ?1 ), typeof( ?2 )", arrParam, 2, &rowSet ) == SQLResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rowSet._listRow.size() );
    SW_EXPECT_EQUAL( int64( 0 ), rowSet._listRow[0][0]._integer );
    SW_EXPECT_EQUAL( int64( 0 ), rowSet._listRow[0][1]._integer );
    SW_EXPECT_TRUE( rowSet._listRow[0][2].getText() == "text" );
    SW_EXPECT_TRUE( rowSet._listRow[0][3].getText() == "blob" );
}

/** @brief 매개변수는 값이지 문장이 아니다 — SQL 구문이 든 글이 그대로 저장되고 표가 남는다. */
SW_TEST_CASE( SQLiteDriverTest, HostileTextStaysData )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    const string_view hostile    = "x'); DROP TABLE t; --";
    const SQLValue    arrParam[] = { SQLValue::makeText( hostile ), SQLValue::makeInt64( 1 ) };
    SW_REQUIRE_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrParam, 2, nullptr ) == SQLResult::Ok );
    SQLRowSet rowSet;
    SW_REQUIRE_TRUE( connection->execute( "SELECT a FROM t", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rowSet._listRow.size() );
    SW_EXPECT_TRUE( rowSet._listRow[0][0].getText() == hostile );
}

/**
 * @brief `execute` 는 문 하나만 받는다.
 * @details 결함: 준비 단계의 나머지 글(pzTail)을 보지 않아 둘째 문을 말없이 버렸다 — 적은 SQL 과 다른 일을 하고도 Ok 였다. PostgreSQL 은 거절하므로 드라이버 사이에서도 달랐다.
 */
SW_TEST_CASE( SQLiteDriverTest, ExecuteRefusesASecondStatement )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    SW_EXPECT_TRUE( connection->execute( "SELECT 1; DROP TABLE t", nullptr, 0, nullptr ) == SQLResult::Error );
    SW_EXPECT_TRUE( connection->execute( "SELECT 1;  ", nullptr, 0, nullptr ) == SQLResult::Ok ); // 끝의 쌍반점 · 공백은 둘째 문이 아니다
    SW_EXPECT_TRUE( connection->execute( "", nullptr, 0, nullptr ) == SQLResult::Error );         // 문이 없다
    SW_EXPECT_TRUE( connection->execute( "SELECT a FROM t", nullptr, 0, nullptr ) == SQLResult::Ok );
    SW_EXPECT_TRUE( connection->isAlive() );
}

/**
 * @brief 값 수가 자리 수와 다르면 오류다.
 * @details 결함: 모자란 칸은 NULL 로, 남는 값은 결과를 보지 않은 bind 에서 버려져 다른 문장이 말없이 돌았다.
 */
SW_TEST_CASE( SQLiteDriverTest, ParameterCountMustMatch )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    const SQLValue arrParam[] = { SQLValue::makeText( "k" ), SQLValue::makeInt64( 1 ), SQLValue::makeInt64( 2 ) };
    SW_EXPECT_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrParam, 1, nullptr ) == SQLResult::Error );
    SW_EXPECT_TRUE( string_view( connection->getLastErrorText() ).empty() == false );
    SW_EXPECT_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrParam, 3, nullptr ) == SQLResult::Error );
    SW_EXPECT_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrParam, 2, nullptr ) == SQLResult::Ok );
    SW_EXPECT_TRUE( string_view( connection->getLastErrorText() ).empty() ); // 성공한 호출이 비운다
}

/**
 * @brief 고유 키 위반만 Constraint 다.
 * @details 결함: 모든 제약 위반이 Constraint 여서 NOT NULL 도 서비스 저장소에서 Conflict(다시 하면 된다)로 올라갔다. PostgreSQL 은 23505 만 그렇다.
 */
SW_TEST_CASE( SQLiteDriverTest, OnlyUniqueViolationIsConstraint )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    const SQLValue arrParam[] = { SQLValue::makeText( "k" ), SQLValue::makeInt64( 1 ) };
    SW_REQUIRE_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrParam, 2, nullptr ) == SQLResult::Ok );
    SW_EXPECT_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrParam, 2, nullptr ) == SQLResult::Constraint );
    const SQLValue arrNull[] = { SQLValue::makeText( "other" ), SQLValue{} };
    SW_EXPECT_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrNull, 2, nullptr ) == SQLResult::Error );
    // 실패한 문장은 캐시에 남아 다음 호출에 다시 쓰인다 — 리셋을 빠뜨리면 이 호출이 진다.
    const SQLValue arrFresh[] = { SQLValue::makeText( "fresh" ), SQLValue::makeInt64( 2 ) };
    SW_EXPECT_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrFresh, 2, nullptr ) == SQLResult::Ok );
    SW_EXPECT_TRUE( connection->isAlive() ); // 제약 위반은 끊김이 아니다
}

/**
 * @brief 바뀐 행 수는 그 문의 것이다.
 * @details 결함: `sqlite3_changes64` 는 앞선 쓰기의 수를 그대로 돌려주어 SELECT 의 수가 낡은 값이었다.
 */
SW_TEST_CASE( SQLiteDriverTest, AffectedRowCountBelongsToTheStatement )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    const SQLValue arrParam[] = { SQLValue::makeText( "k" ), SQLValue::makeInt64( 1 ) };
    SQLRowSet      rowSet;
    SW_REQUIRE_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrParam, 2, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_EQUAL( int64( 1 ), rowSet._affectedRowCount );
    SW_REQUIRE_TRUE( connection->execute( "SELECT a FROM t", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), rowSet._affectedRowCount );
    const SQLValue arrMissing[] = { SQLValue::makeText( "missing" ) };
    SW_REQUIRE_TRUE( connection->execute( "DELETE FROM t WHERE a = ?", arrMissing, 1, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), rowSet._affectedRowCount );
    SW_REQUIRE_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? ) ON CONFLICT ( a ) DO NOTHING", arrParam, 2, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), rowSet._affectedRowCount ); // 서비스 저장소의 "없어야 한다" 조건이 이 수로 판정한다
}

/**
 * @brief `SQLValue` 로 담을 수 없는 칸은 오류다.
 * @details 결함: REAL 칸을 NULL 로 읽어 값이 말없이 사라졌다.
 */
SW_TEST_CASE( SQLiteDriverTest, RealColumnIsRefusedNotNulled )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    SQLRowSet rowSet;
    SW_EXPECT_TRUE( connection->execute( "SELECT 1.5", nullptr, 0, &rowSet ) == SQLResult::Error );
    SW_EXPECT_TRUE( rowSet._listRow.empty() );
    SW_EXPECT_TRUE( connection->execute( "SELECT 1.5", nullptr, 0, nullptr ) == SQLResult::Ok ); // 행을 받지 않으면 읽지 않는다
}

SW_TEST_CASE( SQLiteDriverTest, ScriptRunsManyStatementsAndReportsFailure )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    SW_EXPECT_TRUE( connection->executeScript( "" ) == SQLResult::Ok ); // 락 문이 빈 방언이 그대로 부른다
    SW_EXPECT_TRUE( connection->executeScript( "CREATE TABLE u ( x ); INSERT INTO u VALUES ( 1 ); INSERT INTO u VALUES ( 2 );" ) == SQLResult::Ok );
    SQLRowSet rowSet;
    SW_REQUIRE_TRUE( connection->execute( "SELECT count( * ) FROM u", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_EQUAL( int64( 2 ), rowSet._listRow[0][0]._integer );

    SW_EXPECT_TRUE( connection->executeScript( "NOT SQL AT ALL" ) == SQLResult::Error );
    SW_EXPECT_TRUE( string_view( connection->getLastErrorText() ).empty() == false );
    SW_EXPECT_TRUE( connection->isAlive() );
}

SW_TEST_CASE( SQLiteDriverTest, RollbackUndoesTheTransaction )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    SW_REQUIRE_TRUE( connection->executeScript( connection->getDialect()._pBeginWrite ) == SQLResult::Ok );
    const SQLValue arrParam[] = { SQLValue::makeText( "k" ), SQLValue::makeInt64( 1 ) };
    SW_REQUIRE_TRUE( connection->execute( "INSERT INTO t ( a, b ) VALUES ( ?, ? )", arrParam, 2, nullptr ) == SQLResult::Ok );
    SW_REQUIRE_TRUE( connection->executeScript( "ROLLBACK" ) == SQLResult::Ok );
    SQLRowSet rowSet;
    SW_REQUIRE_TRUE( connection->execute( "SELECT a FROM t", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_EXPECT_TRUE( rowSet._listRow.empty() );
}

/** @brief 연결마다 설정 문장이 돌아 외래 키가 켜져 있다(메모리 DB 도 같은 길). */
SW_TEST_CASE( SQLiteDriverTest, ConnectionSetupRan )
{
    unique_ptr<ISQLConnection> connection = SQLiteDriverTestUtil::openWithTable();
    SW_REQUIRE_NOT_NULL( connection );
    SQLRowSet rowSet;
    SW_REQUIRE_TRUE( connection->execute( "PRAGMA foreign_keys", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rowSet._listRow.size() );
    SW_EXPECT_EQUAL( int64( 1 ), rowSet._listRow[0][0]._integer );
    SW_EXPECT_TRUE( string_view( connection->getDialect()._pDriverName ) == "sqlite" );
}

SW_TEST_CASE( SQLiteDriverTest, OpenFailuresExplainAndLeaveNoConnection )
{
    ISQLDriver* pDriver = SQLiteDriverTestUtil::findDriver();
    SW_REQUIRE_NOT_NULL( pDriver );
    string error;
    error = describeError( pDriver->openConnection( "", "" ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );

    error.clear();
    const string garbagePath = test::makeScratchPath( "not_a_database.db" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( garbagePath, "this file is long enough that SQLite reads a header from it and refuses it as not a database at all." ) );
    error = describeError( pDriver->openConnection( garbagePath, "" ) ); // 설정 문장이 파일 머리를 읽다 진다 — 핸들을 닫고 까닭을 돌려준다
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( garbagePath ) != string::npos );

    error.clear();
    error = describeError( pDriver->openConnection( FileUtil::joinPath( test::makeScratchPath( "no_such_folder" ), "x.db" ), "" ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
}
