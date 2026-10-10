#include "pch.h"

#include "EngineTest/GameFramework/Kits/Storage/PostgresTestSchema.h"
#include "EngineTest/GameFramework/Kits/Storage/SQLDriverContract.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/PostgresDriver.h"

// SQL 드라이버(PostgreSQL) — SQLite 와 같은 드라이버 계약 다섯 + 직렬화 실패(SERIALIZABLE 두 연결). 서버가 있어야 돈다(SW_TEST_POSTGRES_URL).

using namespace sw;

SW_TEST_REQUIRES_ENVIRONMENT( SQLDriverPostgresTest, "SW_TEST_POSTGRES_URL", "needs a PostgreSQL server" );

namespace
{
    struct PostgresDriverFixture
    {
        test::PostgresTestSchema   _schema;
        unique_ptr<ISQLConnection> _connection;

        PostgresDriverFixture()
            : _schema{}
            , _connection{}
        {
            if ( _schema.isReady() == false )
                return;
            string error;
            _connection = PostgresDriver::getInstance().openConnection( _schema.getConnection(), "", error );
            SW_EXPECT_TRUE_MSG( _connection != nullptr, error.c_str() );
        }

        bool            isReady() const { return _connection != nullptr; }
        ISQLConnection& getConnection() { return *_connection; }
        string          makeTableName( const utf8* pCaseName ) { return string( "drv_" ) + pCaseName; }
    };
} // namespace

SW_SQL_DRIVER_CONTRACT_SUITE( SQLDriverPostgresTest, PostgresDriverFixture )

SW_TEST_CASE( SQLDriverPostgresTest, ConcurrentSerializableWritersReportASerializationFailure )
{
    PostgresDriverFixture fixture;
    SW_ASSERT_TRUE( fixture.isReady() );
    string                     error;
    unique_ptr<ISQLConnection> other = PostgresDriver::getInstance().openConnection( fixture._schema.getConnection(), "", error );
    SW_ASSERT_TRUE_MSG( other != nullptr, error.c_str() );
    ISQLConnection& first = fixture.getConnection();
    SW_ASSERT_TRUE( first.executeScript( "CREATE TABLE counter ( id BIGINT PRIMARY KEY, value BIGINT ); INSERT INTO counter VALUES ( 1, 0 );" ) == SQLResult::Ok );

    SQLRowSet rowSet;
    SW_ASSERT_TRUE( first.executeScript( first.getDialect()._pBeginWrite ) == SQLResult::Ok );
    SW_ASSERT_TRUE( other->executeScript( other->getDialect()._pBeginWrite ) == SQLResult::Ok );
    SW_ASSERT_TRUE( first.execute( "SELECT value FROM counter WHERE id = 1", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_ASSERT_TRUE( other->execute( "SELECT value FROM counter WHERE id = 1", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_ASSERT_TRUE( first.execute( "UPDATE counter SET value = value + 1 WHERE id = 1", nullptr, 0, &rowSet ) == SQLResult::Ok );
    SW_ASSERT_TRUE( first.executeScript( "COMMIT" ) == SQLResult::Ok );
    // 둘째는 읽은 행을 남이 바꿨다 — 갱신이나 COMMIT 에서 직렬화 실패.
    SQLResult result = other->execute( "UPDATE counter SET value = value + 1 WHERE id = 1", nullptr, 0, &rowSet );
    if ( result == SQLResult::Ok )
        result = other->executeScript( "COMMIT" );
    else
        (void)other->executeScript( "ROLLBACK" );
    SW_EXPECT_TRUE( result == SQLResult::SerializationFailure );
    SW_EXPECT_TRUE( other->isAlive() );
}
