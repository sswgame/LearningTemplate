#include "pch.h"

#include "EngineTest/GameFramework/Kits/Storage/PostgresTestSchema.h"
#include "EngineTest/GameFramework/Kits/Storage/SqlDriverContract.h"

#include "GameFramework/Kits/Storage/Server/SqlStore/Driver/Postgres/PostgresDriver.h"

// SQL 드라이버(PostgreSQL) — SQLite 와 같은 드라이버 계약 다섯 + 직렬화 실패(SERIALIZABLE 두 연결). 서버가 있어야 돈다(SW_TEST_POSTGRES_URL).

using namespace sw;

SW_TEST_REQUIRES_ENVIRONMENT( SqlDriverPostgresTest, "SW_TEST_POSTGRES_URL", "needs a PostgreSQL server" );

namespace
{
    struct PostgresDriverFixture
    {
        test::PostgresTestSchema   _schema;
        unique_ptr<ISqlConnection> _connection;

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
        ISqlConnection& getConnection() { return *_connection; }
        string          makeTableName( const utf8* pCaseName ) { return string( "drv_" ) + pCaseName; }
    };
} // namespace

SW_SQL_DRIVER_CONTRACT_SUITE( SqlDriverPostgresTest, PostgresDriverFixture )

SW_TEST_CASE( SqlDriverPostgresTest, ConcurrentSerializableWritersReportASerializationFailure )
{
    PostgresDriverFixture fixture;
    SW_ASSERT_TRUE( fixture.isReady() );
    string                     error;
    unique_ptr<ISqlConnection> other = PostgresDriver::getInstance().openConnection( fixture._schema.getConnection(), "", error );
    SW_ASSERT_TRUE_MSG( other != nullptr, error.c_str() );
    ISqlConnection& first = fixture.getConnection();
    SW_ASSERT_TRUE( first.executeScript( "CREATE TABLE counter ( id BIGINT PRIMARY KEY, value BIGINT ); INSERT INTO counter VALUES ( 1, 0 );" ) == SqlResult::Ok );

    SqlRowSet rowSet;
    SW_ASSERT_TRUE( first.executeScript( first.getDialect()._pBeginWrite ) == SqlResult::Ok );
    SW_ASSERT_TRUE( other->executeScript( other->getDialect()._pBeginWrite ) == SqlResult::Ok );
    SW_ASSERT_TRUE( first.execute( "SELECT value FROM counter WHERE id = 1", nullptr, 0, &rowSet ) == SqlResult::Ok );
    SW_ASSERT_TRUE( other->execute( "SELECT value FROM counter WHERE id = 1", nullptr, 0, &rowSet ) == SqlResult::Ok );
    SW_ASSERT_TRUE( first.execute( "UPDATE counter SET value = value + 1 WHERE id = 1", nullptr, 0, &rowSet ) == SqlResult::Ok );
    SW_ASSERT_TRUE( first.executeScript( "COMMIT" ) == SqlResult::Ok );
    // 둘째는 읽은 행을 남이 바꿨다 — 갱신이나 COMMIT 에서 직렬화 실패.
    SqlResult result = other->execute( "UPDATE counter SET value = value + 1 WHERE id = 1", nullptr, 0, &rowSet );
    if ( result == SqlResult::Ok )
        result = other->executeScript( "COMMIT" );
    else
        (void)other->executeScript( "ROLLBACK" );
    SW_EXPECT_TRUE( result == SqlResult::SerializationFailure );
    SW_EXPECT_TRUE( other->isAlive() );
}
