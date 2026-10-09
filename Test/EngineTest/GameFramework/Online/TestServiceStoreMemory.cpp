#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "EngineTest/GameFramework/Online/ServiceStoreContract.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

// 메모리 서비스 저장소 — 계약 일곱(로그 문맥 포함) + 실패 주입(커밋 거절 · 응답 유실 · 읽기 거절), 앞 둘 · 데이터 하나, 내린 뒤 맡긴 일.

using namespace sw;

namespace
{
    struct MemoryStoreFixture
    {
        MemoryServiceDatabase _database;
        MemoryServiceStore    _store{ &_database };

        IServiceStore& getStore() { return _store; }

        hashed_string makeTableName( const utf8* pCaseName )
        {
            string name{ "contract_" };
            name += StringUtil::toLower( pCaseName );
            return hashed_string( name.c_str() );
        }
    };

    struct TestServiceStoreMemoryInternal
    {
        static const hashed_string& getTable()
        {
            static const hashed_string s_table{ "table_a" };
            return s_table;
        }

        static vector<uint8> makeBytes( uint8 value ) { return vector<uint8>{ value }; }
    };

    /** @brief 레코드 "k" 를 읽는 일 — 완료 수와 결과를 시험 스레드에 돌려준다. */
    class MemoryStoreCountingWork final : public IServiceStoreWork
    {
    public:
        MemoryStoreCountingWork( int32* pCompleted, ServiceStoreResult* pOutResult )
            : _pCompleted{ pCompleted }
            , _pOutResult{ pOutResult }
            , _result{ ServiceStoreResult::Ok }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            ServiceRecord record;
            _result = connection.readRecord( TestServiceStoreMemoryInternal::getTable(), "k", record );
        }

        void complete() override
        {
            ++*_pCompleted;
            *_pOutResult = _result;
        }

    private:
        int32*              _pCompleted;
        ServiceStoreResult* _pOutResult;
        ServiceStoreResult  _result;
    };
} // namespace

SW_SERVICE_STORE_CONTRACT_SUITE( ServiceStoreMemoryTest, MemoryStoreFixture )

SW_TEST_CASE( ServiceStoreMemoryTest, InjectedFaultsRejectOrLoseTheReply )
{
    const hashed_string&  table = TestServiceStoreMemoryInternal::getTable();
    MemoryServiceDatabase database;
    ServiceTransaction    write;
    write.put( table, "k", TestServiceStoreMemoryInternal::makeBytes( 1 ), ServiceRecord::kAbsentVersion );
    const uint64 emptyHash = database.computeContentHash();

    database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( database.commit( write ) == ServiceStoreResult::Unavailable );
    SW_EXPECT_EQUAL( 0, database.countRecords( table ) );
    SW_EXPECT_EQUAL( emptyHash, database.computeContentHash() ); // 거절된 커밋은 아무것도 바꾸지 않았다

    database.armFault( ServiceStoreFault::LoseCommitReply );
    SW_EXPECT_TRUE( database.commit( write ) == ServiceStoreResult::Unavailable );
    SW_EXPECT_EQUAL( 1, database.countRecords( table ) ); // 적용했지만 응답을 잃었다
    SW_EXPECT_TRUE( database.commit( write ) == ServiceStoreResult::Conflict );

    database.armFault( ServiceStoreFault::RejectRead, 1 );
    ServiceRecord record;
    SW_EXPECT_TRUE( database.readRecord( table, "k", record ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( database.readRecord( table, "k", record ) == ServiceStoreResult::Unavailable );
    SW_EXPECT_FALSE( database.isFaultArmed() );
}

SW_TEST_CASE( ServiceStoreMemoryTest, CompletionsWaitForPollAndShutdownStillCompletes )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    frontA{ &database };
    MemoryServiceStore    frontB{ &database }; // 같은 데이터의 두 번째 서버
    int32                 completedA = 0;
    int32                 completedB = 0;
    ServiceStoreResult    resultA    = ServiceStoreResult::Ok;
    ServiceStoreResult    resultB    = ServiceStoreResult::Ok;
    frontA.submit( make_unique<MemoryStoreCountingWork>( &completedA, &resultA ) );
    frontB.submit( make_unique<MemoryStoreCountingWork>( &completedB, &resultB ) );
    SW_EXPECT_EQUAL( 0, completedA ); // poll 전에는 complete 가 돌지 않는다
    SW_EXPECT_EQUAL( 1, frontA.getPendingCount() );
    SW_EXPECT_EQUAL( 1, frontA.pollCompletions() );
    SW_EXPECT_EQUAL( 1, completedA );
    SW_EXPECT_TRUE( resultA == ServiceStoreResult::NotFound );
    SW_EXPECT_EQUAL( 0, completedB ); // 다른 앞의 완료를 가져가지 않는다

    SW_EXPECT_EQUAL( 1, frontB.pollCompletions() );
    frontB.shutdown();
    frontB.submit( make_unique<MemoryStoreCountingWork>( &completedB, &resultB ) );
    SW_EXPECT_EQUAL( 1, frontB.pollCompletions() );
    SW_EXPECT_EQUAL( 2, completedB );
    SW_EXPECT_TRUE( resultB == ServiceStoreResult::Unavailable ); // 내린 뒤 맡긴 일도 정확히 한 번 끝난다
}
