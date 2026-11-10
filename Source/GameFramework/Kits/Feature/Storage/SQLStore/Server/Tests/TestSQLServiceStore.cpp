#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"
#include "Core/Time/TimePoint.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQLMigrationRunner.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/Tests/FakeSQLDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/SQLServiceStore.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Server/IOnlineStoreDriver.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

namespace
{
    /** @brief 실패의 문장과 대상을 이은 글입니다(성공이면 빈 글). 오류 글을 확인하는 테스트가 씁니다. */
    template <typename T>
    sw::string describeError( const sw::expected<T>& result )
    {
        return result.has_value() ? sw::string{} : sw::string( result.error().message() ) + " " + sw::string( result.error().detail() );
    }
} // namespace

// SQLServiceStoreTest · ServiceStoreParityTest — SQL 서비스 저장소: 가짜 드라이버로 문장 · 트랜잭션 · 실패 처리를 보고, 실제 SQLite 가 메모리 구현과 같은 시나리오를 같은 결과로 지나는지 본다.
// 서버 키트 케이스다(Game 타깃의 `App --test`, nogpu). PostgreSQL 은 쓰지 않는다 — 외부 DB 없이 돈다.

using namespace sw;

namespace
{
    const hashed_string kTable{ "parity" };

    /** @brief 저장소 일 하나를 함수 하나로 감쌉니다. */
    template <typename FunctionType>
    class ServiceStoreTestJob final : public IServiceStoreJob
    {
    public:
        ServiceStoreTestJob( FunctionType function, int32& outCompleteCount, int32& outCancelledCount )
            : _function{ std::move( function ) }
            , _outCompleteCount{ outCompleteCount }
            , _outCancelledCount{ outCancelledCount }
        {
        }

        void run( IServiceStoreConnection& connection ) override { _function( connection ); }
        void complete() override
        {
            ++_outCompleteCount;
            if ( isCancelled() )
                ++_outCancelledCount;
        }

    private:
        FunctionType _function;
        int32&       _outCompleteCount;
        int32&       _outCancelledCount;
    };

    struct SQLServiceStoreTestUtil
    {
        static vector<uint8> makeBytes( string_view text ) { return vector<uint8>( text.begin(), text.end() ); }

        /** @brief 일을 맡기고 끝날 때까지 거둡니다(시한 10 초). 저장소 스레드에서 도는 @p function 이 쓴 값은 잠금을 지나 보입니다. */
        template <typename FunctionType>
        [[nodiscard]] static bool run( IServiceStore& store, FunctionType function )
        {
            int32 completeCount  = 0;
            int32 cancelledCount = 0;
            store.submit( sw::make_unique<ServiceStoreTestJob<FunctionType>>( std::move( function ), completeCount, cancelledCount ) );
            (void)test::waitUntil( [&]()
            {
                (void)store.pollCompletions();
                return completeCount > 0;
            } );
            return completeCount == 1;
        }

        /** @brief @p store 에서 트랜잭션 하나를 커밋합니다. */
        static ServiceStoreResult commit( IServiceStore& store, const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr )
        {
            ServiceStoreResult result = ServiceStoreResult::Invalid;
            const bool         bDone  = run( store, [&]( IServiceStoreConnection& connection )
                     { result = connection.commit( transaction, pOutInfo ); } );
            SW_EXPECT_TRUE( bDone );
            return result;
        }

        static ServiceStoreResult read( IServiceStore& store, string_view key, ServiceRecord& outRecord )
        {
            ServiceStoreResult result = ServiceStoreResult::Invalid;
            const bool         bDone  = run( store, [&]( IServiceStoreConnection& connection )
                     { result = connection.readRecord( kTable, key, outRecord ); } );
            SW_EXPECT_TRUE( bDone );
            return result;
        }
    };

    /**
     * @brief 가짜 드라이버 위의 저장소입니다. 마이그레이션 폴더는 케이스 폴더에 쓴 한 파일이고, 워커는 하나입니다(문장 순서가 곧 맡긴 순서).
     * @details 기동의 문장은 지운다 — 케이스는 그 뒤의 문장만 본다.
     */
    struct FakeStoreFixture
    {
        test::ScopedScratchDomain _domain{};
        test::FakeSQLDriver       _driver{};
        SQLDriverRegistration     _registration{ _driver };
        SQLServiceStore           _store{};

        FakeStoreFixture()
        {
            const string folder = _domain.makeDiskFolder( "sql" );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, SQLMigrationRunner::kListFileName ), "<SQLMigrationList><Migration _name=\"0001_fake\" /></SQLMigrationList>" ) );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "0001_fake.sql" ), "CREATE FAKE" ) );
            _driver.addRows( test::FakeSQLDriver::kNextCommitVersion, { vector<SQLValue>{ SQLValue::makeInt64( 77 ) } }, 1 );
            SQLConnectionPoolConfig settings;
            settings._connection         = "fake";
            settings._secret             = "not logged";
            settings._workerCount        = 1;
            const expected<void> started = _store.initialize( test::FakeSQLDriver::kName, settings, string( test::ScopedScratchDomain::kDomain ) + "/sql", UnixMilliseconds{ 1 } );
            SW_EXPECT_TRUE_MSG( started.has_value(), describeError( started ) );
            _driver.clearLog();
        }

        ~FakeStoreFixture() { _store.shutdown(); } // 워커의 연결이 드라이버 등록보다 먼저 사라진다
    };

    /** @brief 실제 SQLite 파일 위의 저장소입니다. */
    struct SQLiteStoreFixture
    {
        SQLServiceStore _store{};

        SQLiteStoreFixture()
        {
            SQLConnectionPoolConfig settings;
            settings._connection         = test::makeScratchPath( "store.db" );
            settings._workerCount        = 2;
            const expected<void> started = _store.initialize( "sqlite", settings, SQLServiceStore::kMigrationFolder, UnixMilliseconds{ 1 } );
            SW_EXPECT_TRUE_MSG( started.has_value(), describeError( started ) );
        }
    };

    /**
     * @struct ServiceStoreParity
     * @brief 저장소 구현 사이의 약속을 글 줄로 적어 견줍니다(버전 값은 구현마다 빈틈이 달라 크기 비교로 적는다). 메모리와 SQL 이 같은 줄을 내야 합니다.
     * @details 예전에는 `SQLServiceStore.h` 가 "메모리와 같은 계약 테스트" 를 말했지만 그런 파일(`ServiceStoreContract.h`)이 없었다.
     */
    struct ServiceStoreParity
    {
        static void addLine( vector<string>& inoutListLine, string_view step, ServiceStoreResult result, int32 detail = 0 )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "%#: %# %#", step, static_cast<int32>( result ), detail );
            inoutListLine.push_back( string( textBuilder.view() ) );
        }

        /** @brief 커밋하고 결과와 어긋난 쓰기 번호를 적습니다. 커밋 정보는 @p outInfo 에 남습니다. */
        static void recordCommit( vector<string>& inoutListLine, IServiceStoreConnection& connection, string_view step, const ServiceTransaction& transaction, ServiceCommitInfo& outInfo )
        {
            outInfo                         = ServiceCommitInfo{};
            const ServiceStoreResult result = connection.commit( transaction, &outInfo );
            addLine( inoutListLine, step, result, outInfo._conflictIndex );
        }

        /** @brief 읽고 결과와 바이트 수를 적습니다. 레코드는 @p outRecord 에 남습니다. */
        static void recordRead( vector<string>& inoutListLine, IServiceStoreConnection& connection, string_view step, const hashed_string& table, string_view key, ServiceRecord& outRecord )
        {
            const ServiceStoreResult result = connection.readRecord( table, key, outRecord );
            addLine( inoutListLine, step, result, static_cast<int32>( outRecord._bytes.size() ) );
        }

        static vector<string> runScenario( IServiceStoreConnection& connection )
        {
            vector<string>    listLine;
            ServiceRecord     record;
            ServiceCommitInfo info;

            recordRead( listLine, connection, "read missing", kTable, "a", record );
            listLine.push_back( record._version == ServiceRecord::kAbsentVersion ? "absent version" : "some version" );

            ServiceTransaction create;
            create.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "1" ), ServiceRecord::kAbsentVersion );
            recordCommit( listLine, connection, "create", create, info );
            const uint64 firstVersion = info._commitVersion;
            listLine.push_back( firstVersion > 0 ? "version given" : "no version" );
            recordCommit( listLine, connection, "create again", create, info );

            recordRead( listLine, connection, "read", kTable, "a", record );
            listLine.push_back( record._version == firstVersion ? "read the commit version" : "read another version" );
            listLine.push_back( string( reinterpret_cast<const utf8*>( record._bytes.data() ), record._bytes.size() ) );

            ServiceTransaction update;
            update.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "2" ), firstVersion );
            recordCommit( listLine, connection, "update", update, info );
            const uint64 secondVersion = info._commitVersion;
            listLine.push_back( secondVersion > firstVersion ? "version rose" : "version did not rise" );
            recordCommit( listLine, connection, "stale update", update, info );

            ServiceTransaction mixed;
            mixed.put( kTable, "fresh", SQLServiceStoreTestUtil::makeBytes( "x" ) );
            mixed.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "3" ), firstVersion );
            recordCommit( listLine, connection, "mixed", mixed, info );
            recordRead( listLine, connection, "mixed left nothing", kTable, "fresh", record );

            ServiceTransaction require;
            require.requireVersion( kTable, "a", secondVersion );
            require.put( kTable, "b", SQLServiceStoreTestUtil::makeBytes( "b" ) );
            recordCommit( listLine, connection, "require", require, info );
            ServiceTransaction staleRequire;
            staleRequire.requireVersion( kTable, "a", firstVersion );
            recordCommit( listLine, connection, "stale require", staleRequire, info );
            ServiceTransaction requireAbsent;
            requireAbsent.requireVersion( kTable, "nothing", ServiceRecord::kAbsentVersion );
            recordCommit( listLine, connection, "require absent", requireAbsent, info );

            ServiceTransaction staleErase;
            staleErase.erase( kTable, "b", firstVersion );
            recordCommit( listLine, connection, "stale erase", staleErase, info );
            ServiceTransaction eraseAbsentButThere;
            eraseAbsentButThere.erase( kTable, "b", ServiceRecord::kAbsentVersion );
            recordCommit( listLine, connection, "erase expecting absent", eraseAbsentButThere, info );
            ServiceTransaction eraseMissing;
            eraseMissing.erase( kTable, "nothing" );
            recordCommit( listLine, connection, "erase missing", eraseMissing, info );
            ServiceTransaction erase;
            erase.erase( kTable, "b" );
            recordCommit( listLine, connection, "erase", erase, info );
            recordRead( listLine, connection, "erased", kTable, "b", record );

            // 지웠다 다시 만들어도 옛 버전을 받지 않는다(ABA 없음).
            ServiceTransaction recreate;
            recreate.put( kTable, "b", SQLServiceStoreTestUtil::makeBytes( "again" ), ServiceRecord::kAbsentVersion );
            recordCommit( listLine, connection, "recreate", recreate, info );
            listLine.push_back( info._commitVersion > secondVersion ? "new version" : "old version" );

            ServiceTransaction empty;
            empty.put( kTable, "emptybytes", vector<uint8>{} );
            recordCommit( listLine, connection, "empty bytes", empty, info );
            recordRead( listLine, connection, "read empty bytes", kTable, "emptybytes", record );

            for ( const utf8* pKey : { "k/1", "k/2", "k/3", "l/1", "k0" } )
            {
                ServiceTransaction seed;
                seed.put( kTable, pKey, SQLServiceStoreTestUtil::makeBytes( pKey ) );
                recordCommit( listLine, connection, pKey, seed, info );
            }
            noteList( listLine, connection, "first page", "k/", "", 2, false );
            noteList( listLine, connection, "second page", "k/", "k/2", 2, false );
            noteList( listLine, connection, "last two", "k/", "", 2, true );
            noteList( listLine, connection, "before cursor", "k/", "k/2", 5, true );
            noteList( listLine, connection, "no match", "zzz/", "", 5, false );
            noteList( listLine, connection, "no match down", "zzz/", "", 5, true );
            noteList( listLine, connection, "other table", "", "", 5, false, hashed_string{ "paritytwo" } );

            // 키 규칙은 DB 에 닿기 전에 거른다.
            recordRead( listLine, connection, "bad key read", kTable, "Bad Key'; DROP TABLE sw_record; --", record );
            recordRead( listLine, connection, "empty table read", hashed_string{}, "a", record );
            ServiceTransaction badKey;
            badKey.put( kTable, "Bad Key'; DROP TABLE sw_record; --", SQLServiceStoreTestUtil::makeBytes( "x" ) );
            recordCommit( listLine, connection, "bad key commit", badKey, info );
            ServiceTransaction twice;
            twice.put( kTable, "dup", SQLServiceStoreTestUtil::makeBytes( "1" ) );
            twice.put( kTable, "dup", SQLServiceStoreTestUtil::makeBytes( "2" ) );
            recordCommit( listLine, connection, "same key twice", twice, info );
            recordCommit( listLine, connection, "empty transaction", ServiceTransaction{}, info );
            vector<ServiceRecord> listRecord;
            addLine( listLine, "list zero", connection.listRecords( kTable, "", "", 0, false, listRecord ) );
            return listLine;
        }

    private:
        static void noteList( vector<string>& inoutListLine, IServiceStoreConnection& connection, string_view step, string_view prefix, string_view cursor, int32 maxCount, bool bDescending,
                              const hashed_string& table = kTable )
        {
            vector<ServiceRecord>                    listRecord;
            const ServiceStoreResult                 result = connection.listRecords( table, prefix, cursor, maxCount, bDescending, listRecord );
            StringBuilder<constant::buffer::kMax256> lineBuilder;
            lineBuilder.appendFormat( "%#: %#", step, static_cast<int32>( result ) );
            string line{ lineBuilder.view() };
            for ( const ServiceRecord& record : listRecord )
            {
                line += " " + record._key + "=" + string( reinterpret_cast<const utf8*>( record._bytes.data() ), record._bytes.size() );
            }
            inoutListLine.push_back( line );
        }
    };
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// 메모리 구현과 같은 결과
// ---------------------------------------------------------------------------------------------------------------------

SW_TEST_CASE( ServiceStoreParityTest, SQLiteAnswersLikeMemory, ._timeoutSeconds = 60 )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    memory{ &database };
    vector<string>        listMemoryLine;
    SW_REQUIRE_TRUE( SQLServiceStoreTestUtil::run( memory, [&]( IServiceStoreConnection& connection )
    { listMemoryLine = ServiceStoreParity::runScenario( connection ); } ) );

    SQLiteStoreFixture fixture;
    vector<string>     listSQLLine;
    SW_REQUIRE_TRUE( SQLServiceStoreTestUtil::run( fixture._store, [&]( IServiceStoreConnection& connection )
    { listSQLLine = ServiceStoreParity::runScenario( connection ); } ) );

    SW_REQUIRE_EQUAL( listMemoryLine.size(), listSQLLine.size() );
    for ( size_t index = 0; index < listMemoryLine.size(); ++index )
    {
        SW_EXPECT_EQUAL( listMemoryLine[index], listSQLLine[index] );
    }
}

/** @brief 워커 둘이 같은 키의 "없어야 한다" 를 다투면 정확히 하나만 이긴다 — 멱등 기록이 기대는 약속이다. 지는 쪽은 Unavailable 이 아니라 Conflict 다. */
SW_TEST_CASE( ServiceStoreParityTest, ConcurrentCreatesHaveOneWinner, ._timeoutSeconds = 60 )
{
    SQLiteStoreFixture         fixture;
    constexpr int32            kWorkCount = 16;
    vector<ServiceStoreResult> listResult( kWorkCount, ServiceStoreResult::Invalid );
    int32                      completeCount  = 0;
    int32                      cancelledCount = 0;
    for ( ServiceStoreResult& result : listResult )
    {
        auto function = [&result]( IServiceStoreConnection& connection )
        {
            ServiceTransaction transaction;
            transaction.put( kTable, "only", SQLServiceStoreTestUtil::makeBytes( "x" ), ServiceRecord::kAbsentVersion );
            result = connection.commit( transaction );
        };
        fixture._store.submit( sw::make_unique<ServiceStoreTestJob<decltype( function )>>( std::move( function ), completeCount, cancelledCount ) );
    }
    (void)test::waitUntil( [&]()
    {
        (void)fixture._store.pollCompletions();
        return completeCount >= kWorkCount;
    }, 45.0 );
    SW_REQUIRE_EQUAL( kWorkCount, completeCount );
    int32 okCount       = 0;
    int32 conflictCount = 0;
    for ( const ServiceStoreResult result : listResult )
    {
        okCount += result == ServiceStoreResult::Ok ? 1 : 0;
        conflictCount += result == ServiceStoreResult::Conflict ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 1, okCount );
    SW_EXPECT_EQUAL( kWorkCount - 1, conflictCount );
}

// ---------------------------------------------------------------------------------------------------------------------
// 문장과 트랜잭션 — 가짜 드라이버
// ---------------------------------------------------------------------------------------------------------------------

SW_TEST_CASE( SQLServiceStoreTest, CommitWrapsWritesInOneTransaction )
{
    FakeStoreFixture   fixture;
    ServiceTransaction transaction;
    transaction.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "1" ) );
    transaction.put( kTable, "b", SQLServiceStoreTestUtil::makeBytes( "2" ), ServiceRecord::kAbsentVersion );
    transaction.erase( kTable, "c" );
    ServiceCommitInfo info;
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction, &info ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( uint64( 77 ), info._commitVersion );

    const vector<string> listSQL = fixture._driver.getSQLLog();
    SW_REQUIRE_EQUAL( size_t( 6 ), listSQL.size() );
    SW_EXPECT_EQUAL( string( "BEGIN WRITE" ), listSQL[0] );
    SW_EXPECT_EQUAL( string( test::FakeSQLDriver::kNextCommitVersion ), listSQL[1] );
    SW_EXPECT_TRUE( listSQL[2].find( "DO UPDATE" ) != string::npos );
    SW_EXPECT_TRUE( listSQL[3].find( "DO NOTHING" ) != string::npos );
    SW_EXPECT_TRUE( listSQL[4].find( "DELETE" ) != string::npos );
    SW_EXPECT_EQUAL( string( "COMMIT" ), listSQL[5] );
}

/** @brief 키와 바이트는 문장이 아니라 매개변수로 간다 — 문장은 상수 그대로다(키 규칙을 지나지 않는 목록 접두어 · 커서도). */
SW_TEST_CASE( SQLServiceStoreTest, ValuesTravelAsParameters )
{
    FakeStoreFixture  fixture;
    const string_view hostile = "x'; DROP TABLE sw_record; --";
    SW_REQUIRE_TRUE( SQLServiceStoreTestUtil::run( fixture._store, [&]( IServiceStoreConnection& connection )
    {
        vector<ServiceRecord> listRecord;
        (void)connection.listRecords( kTable, hostile, hostile, 3, false, listRecord ); // 문장만 본다
        (void)connection.listRecords( kTable, hostile, hostile, 3, true, listRecord );
    } ) );
    const vector<test::FakeSQLDriver::Statement> listStatement = fixture._driver.getStatements();
    SW_REQUIRE_EQUAL( size_t( 2 ), listStatement.size() );
    for ( const test::FakeSQLDriver::Statement& statement : listStatement )
    {
        SW_EXPECT_TRUE( statement._sql.find( "DROP" ) == string::npos );
    }
    SW_REQUIRE_EQUAL( size_t( 5 ), listStatement[0]._listParam.size() );
    SW_EXPECT_TRUE( listStatement[0]._listParam[0].getText() == "parity" );
    SW_EXPECT_TRUE( listStatement[0]._listParam[1].getText() == hostile );
    SW_EXPECT_TRUE( listStatement[0]._listParam[3].getText() == hostile );
    SW_EXPECT_EQUAL( int64( 3 ), listStatement[0]._listParam[4]._integer );
    SW_REQUIRE_EQUAL( size_t( 4 ), listStatement[1]._listParam.size() );
    SW_EXPECT_TRUE( listStatement[1]._listParam[2].getText() == hostile ); // 내림차순은 커서가 범위 끝이다
}

/** @brief 키 규칙을 어긴 커밋 · 읽기는 DB 에 닿지 않는다. */
SW_TEST_CASE( SQLServiceStoreTest, InvalidKeysNeverReachTheDatabase )
{
    FakeStoreFixture   fixture;
    ServiceTransaction badKey;
    badKey.put( kTable, "Bad'; DROP TABLE sw_record; --", SQLServiceStoreTestUtil::makeBytes( "x" ) );
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, badKey ) == ServiceStoreResult::Invalid );
    ServiceRecord record;
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::read( fixture._store, "Bad'; DROP TABLE sw_record; --", record ) == ServiceStoreResult::Invalid );
    SW_EXPECT_EQUAL( size_t( 0 ), fixture._driver.getSQLLog().size() );
}

SW_TEST_CASE( SQLServiceStoreTest, FailedWriteRollsBackAndNeverCommits )
{
    struct FailureCase
    {
        SQLResult          _driverResult;
        ServiceStoreResult _storeResult;
    };
    const FailureCase arrCase[] = {
        {          SQLResult::Constraint,    ServiceStoreResult::Conflict},
        {SQLResult::SerializationFailure,    ServiceStoreResult::Conflict},
        {                SQLResult::Busy, ServiceStoreResult::Unavailable},
        {      SQLResult::ConnectionLost, ServiceStoreResult::Unavailable},
        {               SQLResult::Error, ServiceStoreResult::Unavailable},
    };
    for ( const FailureCase& failure : arrCase )
    {
        FakeStoreFixture fixture;
        fixture._driver.addResult( "INSERT INTO sw_record", failure._driverResult );
        ServiceTransaction transaction;
        transaction.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "1" ) );
        ServiceCommitInfo info;
        SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction, &info ) == failure._storeResult );
        SW_EXPECT_EQUAL( -1, info._conflictIndex ); // 어느 쓰기인지 모른다
        SW_EXPECT_EQUAL( uint64( 0 ), info._commitVersion );
        SW_EXPECT_EQUAL( 1, fixture._driver.countSQL( "ROLLBACK" ) );
        SW_EXPECT_EQUAL( 0, fixture._driver.countSQL( "COMMIT" ) );
    }
}

/** @brief 바뀐 행이 없으면 Conflict 이고, 그 쓰기의 번호를 알린다. */
SW_TEST_CASE( SQLServiceStoreTest, UnmatchedConditionReportsTheWriteIndex )
{
    FakeStoreFixture fixture;
    fixture._driver.addRows( "UPDATE sw_record", {}, 0 );
    ServiceTransaction transaction;
    transaction.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "1" ) );
    transaction.put( kTable, "b", SQLServiceStoreTestUtil::makeBytes( "2" ), 5 );
    ServiceCommitInfo info;
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction, &info ) == ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 1, info._conflictIndex );
    SW_EXPECT_EQUAL( 1, fixture._driver.countSQL( "ROLLBACK" ) );
}

/** @brief 커밋 버전을 받지 못하면 아무것도 쓰지 않고 되돌린다. 실패의 종류는 쓰기와 같은 규칙으로 올라가고, 행 없는 답은 드라이버 오류다. */
SW_TEST_CASE( SQLServiceStoreTest, CommitVersionFailuresRollBack )
{
    struct FailureCase
    {
        SQLResult          _driverResult;
        ServiceStoreResult _storeResult;
    };
    const FailureCase arrCase[] = {
        {               SQLResult::Error, ServiceStoreResult::Unavailable},
        {          SQLResult::Constraint, ServiceStoreResult::Unavailable},
        {SQLResult::SerializationFailure,    ServiceStoreResult::Conflict},
    };
    ServiceTransaction transaction;
    transaction.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "1" ) );
    for ( const FailureCase& failure : arrCase )
    {
        FakeStoreFixture fixture;
        fixture._driver.addResult( test::FakeSQLDriver::kNextCommitVersion, failure._driverResult );
        SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction ) == failure._storeResult );
        SW_EXPECT_EQUAL( 0, fixture._driver.countSQL( "INSERT" ) );
        SW_EXPECT_EQUAL( 1, fixture._driver.countSQL( "ROLLBACK" ) );
        SW_EXPECT_EQUAL( 0, fixture._driver.countSQL( "COMMIT" ) );
    }
    FakeStoreFixture fixture;
    fixture._driver.addRows( test::FakeSQLDriver::kNextCommitVersion, {} );
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction ) == ServiceStoreResult::Unavailable );
    SW_EXPECT_EQUAL( 1, fixture._driver.countSQL( "ROLLBACK" ) );
}

/**
 * @brief 커밋이 실패하면 트랜잭션을 닫는다.
 * @details 결함: SQLite 의 COMMIT 은 잠금에 지면(SQLITE_BUSY) 트랜잭션을 연 채로 돌아오는데 ROLLBACK 이 없었다. 그 워커의 다음 BEGIN 이 모두 "트랜잭션 안의 트랜잭션" 으로 졌고,
 *          연결은 살아 있으니 다시 열리지도 않아 그 워커가 서버가 내려갈 때까지 Unavailable 만 냈다. 마이그레이션 실행기는 이미 되돌리고 있었다.
 */
SW_TEST_CASE( SQLServiceStoreTest, FailedCommitClosesTheTransaction )
{
    FakeStoreFixture fixture;
    fixture._driver.addResult( "COMMIT", SQLResult::Busy, 1 );
    ServiceTransaction transaction;
    transaction.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "1" ) );
    ServiceCommitInfo info;
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction, &info ) == ServiceStoreResult::Unavailable );
    SW_EXPECT_EQUAL( uint64( 0 ), info._commitVersion ); // 결함: 적용됐는지 모르는 커밋이 버전을 알렸다
    SW_EXPECT_EQUAL( string( "ROLLBACK" ), fixture._driver.getSQLLog().back() );
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction, &info ) == ServiceStoreResult::Ok );
}

/** @brief PostgreSQL 의 직렬화 실패는 COMMIT 에서도 온다 — 적용되지 않았으므로 Conflict(다시 읽고 다시 한다)이고 어느 쓰기인지 모른다. */
SW_TEST_CASE( SQLServiceStoreTest, SerializationFailureAtCommitIsConflict )
{
    FakeStoreFixture fixture;
    fixture._driver.addResult( "COMMIT", SQLResult::SerializationFailure, 1 );
    ServiceTransaction transaction;
    transaction.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "1" ) );
    ServiceCommitInfo info;
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction, &info ) == ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( -1, info._conflictIndex );
    SW_EXPECT_EQUAL( uint64( 0 ), info._commitVersion );
}

SW_TEST_CASE( SQLServiceStoreTest, FailedBeginHasNothingToRollBack )
{
    FakeStoreFixture fixture;
    fixture._driver.addResult( "BEGIN WRITE", SQLResult::Busy, 1 );
    ServiceTransaction transaction;
    transaction.put( kTable, "a", SQLServiceStoreTestUtil::makeBytes( "1" ) );
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( fixture._store, transaction ) == ServiceStoreResult::Unavailable );
    SW_EXPECT_EQUAL( 0, fixture._driver.countSQL( "ROLLBACK" ) );
}

/**
 * @brief 읽기의 실패는 Unavailable 이다 — Conflict 는 커밋의 답이다.
 * @details 결함: 읽기가 직렬화 실패 · 교착을 그대로 Conflict 로 돌려주어, 계약에 없는 답을 받은 서비스가 "기대 버전이 어긋났다" 로 읽었다.
 */
SW_TEST_CASE( SQLServiceStoreTest, ReadFailuresAreUnavailable )
{
    const SQLResult arrResult[] = { SQLResult::SerializationFailure, SQLResult::Constraint, SQLResult::Busy, SQLResult::ConnectionLost, SQLResult::Error };
    for ( const SQLResult result : arrResult )
    {
        FakeStoreFixture fixture;
        fixture._driver.addResult( "SELECT", result );
        ServiceRecord record;
        SW_EXPECT_TRUE( SQLServiceStoreTestUtil::read( fixture._store, "a", record ) == ServiceStoreResult::Unavailable );
        ServiceStoreResult listResult = ServiceStoreResult::Ok;
        SW_REQUIRE_TRUE( SQLServiceStoreTestUtil::run( fixture._store, [&]( IServiceStoreConnection& connection )
        {
            vector<ServiceRecord> listRecord;
            listResult = connection.listRecords( kTable, "", "", 1, false, listRecord );
        } ) );
        SW_EXPECT_TRUE( listResult == ServiceStoreResult::Unavailable );
    }
}

/** @brief 종료 뒤 맡긴 일은 닫힌 연결로 돌고 완료를 받으며 취소로 표시된다(`IServiceStoreJob` 의 약속). */
SW_TEST_CASE( SQLServiceStoreTest, WorkAfterShutdownIsCancelled )
{
    FakeStoreFixture fixture;
    fixture._store.shutdown();
    int32              completeCount  = 0;
    int32              cancelledCount = 0;
    ServiceStoreResult result         = ServiceStoreResult::Ok;
    auto               function       = [&result]( IServiceStoreConnection& connection )
    {
        ServiceRecord record;
        result = connection.readRecord( kTable, "a", record );
    };
    fixture._store.submit( sw::make_unique<ServiceStoreTestJob<decltype( function )>>( std::move( function ), completeCount, cancelledCount ) );
    SW_EXPECT_EQUAL( 1, fixture._store.pollCompletions() );
    SW_EXPECT_EQUAL( 1, completeCount );
    SW_EXPECT_EQUAL( 1, cancelledCount );
    SW_EXPECT_TRUE( result == ServiceStoreResult::Unavailable );
    SW_EXPECT_EQUAL( 0, fixture._store.getPendingCount() );
}

// ---------------------------------------------------------------------------------------------------------------------
// 기동
// ---------------------------------------------------------------------------------------------------------------------

SW_TEST_CASE( SQLServiceStoreTest, InitializeFailuresAreStartupErrors )
{
    SQLConnectionPoolConfig settings;
    settings._connection  = test::makeScratchPath( "init.db" );
    settings._workerCount = 1;
    string error;
    {
        SQLServiceStore store;
        error = describeError( store.initialize( "no_such_driver", settings, SQLServiceStore::kMigrationFolder, UnixMilliseconds{ 1 } ) );
        SW_EXPECT_FALSE( error.empty() );
        SW_EXPECT_TRUE( error.find( "no_such_driver" ) != string::npos ); // 다른 드라이버로 대신하지 않는다
    }
    {
        SQLServiceStore store;
        error.clear();
        settings._workerCount = 0;
        error                 = describeError( store.initialize( "sqlite", settings, SQLServiceStore::kMigrationFolder, UnixMilliseconds{ 1 } ) );
        SW_EXPECT_FALSE( error.empty() );
        SW_EXPECT_TRUE( error.empty() == false );
        settings._workerCount = 1;
    }
    {
        test::FakeSQLDriver         driver;
        const SQLDriverRegistration registration{ driver };
        SQLServiceStore             store;
        error.clear();
        error = describeError( store.initialize( test::FakeSQLDriver::kName, settings, SQLServiceStore::kMigrationFolder, UnixMilliseconds{ 1 } ) ); // 0002 에 "fake" 방언이 없고 공통 파일도 없다
        SW_EXPECT_FALSE( error.empty() );
        SW_EXPECT_TRUE( error.find( test::FakeSQLDriver::kName ) != string::npos );
        SW_EXPECT_EQUAL( 0, driver.getOpenCount() );
    }
}

/** @brief 마이그레이션이 실패하면 풀을 세우지 않는다 — 저장소가 반쯤 선 채 일을 받지 않는다. */
SW_TEST_CASE( SQLServiceStoreTest, FailedMigrationStopsTheStartup )
{
    test::ScopedScratchDomain domain;
    const string              folder = domain.makeDiskFolder( "sql" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, SQLMigrationRunner::kListFileName ), "<SQLMigrationList><Migration _name=\"0001_fake\" /></SQLMigrationList>" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "0001_fake.sql" ), "CREATE FAKE" ) );
    test::FakeSQLDriver         driver;
    const SQLDriverRegistration registration{ driver };
    driver.addResult( "CREATE FAKE", SQLResult::Error );
    SQLConnectionPoolConfig settings;
    settings._connection  = "fake";
    settings._workerCount = 2;
    SQLServiceStore store;
    string          error;
    error = describeError( store.initialize( test::FakeSQLDriver::kName, settings, string( test::ScopedScratchDomain::kDomain ) + "/sql", UnixMilliseconds{ 1 } ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
    SW_EXPECT_EQUAL( 1, driver.getOpenCount() ); // 마이그레이션 연결 하나뿐이다 — 워커가 없다
}

/** @brief 키트의 드라이버 서브시스템이 서버 조립이 찾는 이름으로 공개되고, "memory" 는 프로세스 안에서 데이터를 나눠 쓰며 모르는 이름은 오류다. */
SW_TEST_CASE( SQLServiceStoreTest, StoreDriverSubsystemMakesStores )
{
    IServiceStoreDriver* pDriver = game::findSubsystem<IServiceStoreDriver>();
    SW_REQUIRE_NOT_NULL( pDriver );
    string                    error;
    unique_ptr<IServiceStore> first = pDriver->createServiceStore( "memory", "", "", 1, UnixMilliseconds{ 1 } ).value_or( nullptr );
    SW_REQUIRE_NOT_NULL( first );
    unique_ptr<IServiceStore> second = pDriver->createServiceStore( "memory", "", "", 1, UnixMilliseconds{ 1 } ).value_or( nullptr );
    SW_REQUIRE_NOT_NULL( second );
    ServiceTransaction transaction;
    transaction.put( kTable, "factorytestshared", SQLServiceStoreTestUtil::makeBytes( "1" ) );
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::commit( *first, transaction ) == ServiceStoreResult::Ok );
    ServiceRecord record;
    SW_EXPECT_TRUE( SQLServiceStoreTestUtil::read( *second, "factorytestshared", record ) == ServiceStoreResult::Ok );

    error = describeError( pDriver->createServiceStore( "no_such_driver", "", "", 1, UnixMilliseconds{ 1 } ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "no_such_driver" ) != string::npos );

    expected<unique_ptr<IServiceStore>> sqlite = pDriver->createServiceStore( "sqlite", test::makeScratchPath( "factory.db" ), "", 1, UnixMilliseconds{ 1 } );
    SW_EXPECT_TRUE_MSG( sqlite.has_value() && *sqlite != nullptr, describeError( sqlite ) );
}
