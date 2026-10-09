/**
 * @file ServiceStoreContract.h
 * @brief `IServiceStore` 계약 시험 — 메모리 · SQL(SQLite) · SQL(PostgreSQL) 구현이 같은 케이스를 같은 결과로 통과해야 합니다.
 * @details 스위트 파일이 픽스처(`getStore()` · `makeTableName( 케이스 이름 )`)를 주고 `SW_SERVICE_STORE_CONTRACT_SUITE( 스위트, 픽스처 )` 를 부른다.
 *          표 이름은 케이스마다 달라(`contract_<케이스>`) 한 DB 를 여러 케이스가 써도 섞이지 않는다 — 실 DB 구현은 실행마다 다른 접두를 붙인다.
 *          케이스 몸은 저장소 스레드(SQL 은 풀 워커)에서 돈다 — 거기서는 `SW_EXPECT_*` 를 부르지 않고(시험 틀의 기록은 시험 스레드 것이다)
 *          처음 어긋난 단계 번호만 적어 두고 돌아온다. 시험 스레드가 그 번호를 단언한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/LogContext.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Online/Store/ServiceIdempotency.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

#include "TestFramework/TestFramework.h"

#include <thread>

namespace test
{
    /** @brief 케이스 하나의 문맥 — 표 이름과 처음 어긋난 단계입니다. 저장소 스레드가 쓰고, 일이 거둬진 뒤 시험 스레드가 읽는다. */
    struct ServiceStoreContractContext
    {
        sw::hashed_string _table{};
        int32             _stepIndex{ 0 };
        int32             _failedStep{ -1 }; ///< −1 = 모두 통과

        /** @brief 단계 하나를 적습니다. 어긋나면 그 번호를 남기고 false 입니다(케이스는 바로 돌아간다 — 뒤 단계가 앞 결과를 색인한다). */
        bool recordStep( bool bPassed )
        {
            ++_stepIndex;
            if ( bPassed == false && _failedStep < 0 )
                _failedStep = _stepIndex;
            return bPassed;
        }
    };
} // namespace test

namespace test
{
    /** @brief 시험의 일 하나 — 함수 포인터 하나를 저장소 스레드에서 돌리고, 완료(시험 스레드)를 센다. */
    class ServiceStoreContractWork final : public sw::IServiceStoreWork
    {
    public:
        using RunFunction = void ( * )( sw::IServiceStoreConnection& connection, ServiceStoreContractContext& context );

        ServiceStoreContractWork( RunFunction pRun, ServiceStoreContractContext* pContext, int32* pCompletedCount )
            : _pRun{ pRun }
            , _pContext{ pContext }
            , _pCompletedCount{ pCompletedCount }
        {
        }

        void run( sw::IServiceStoreConnection& connection ) override { _pRun( connection, *_pContext ); }
        void complete() override { ++*_pCompletedCount; }

    private:
        RunFunction                  _pRun;
        ServiceStoreContractContext* _pContext;
        int32*                       _pCompletedCount;
    };
} // namespace test

namespace test
{
    /** @brief 저장소 스레드의 `run` 과 시험 스레드의 `complete` 에서 본 로그 문맥을 적는 일입니다(적은 칸은 거둔 뒤 시험 스레드가 읽는다). */
    class ServiceStoreLogContextWork final : public sw::IServiceStoreWork
    {
    public:
        ServiceStoreLogContextWork( sw::LogContext* pOutSeenInRun, sw::LogContext* pOutSeenInComplete, int32* pCompletedCount )
            : _pOutSeenInRun{ pOutSeenInRun }
            , _pOutSeenInComplete{ pOutSeenInComplete }
            , _pCompletedCount{ pCompletedCount }
        {
        }

        void run( sw::IServiceStoreConnection& connection ) override
        {
            (void)connection;
            *_pOutSeenInRun = sw::LogContext::getCurrent();
        }

        void complete() override
        {
            *_pOutSeenInComplete = sw::LogContext::getCurrent();
            ++*_pCompletedCount;
        }

    private:
        sw::LogContext* _pOutSeenInRun;
        sw::LogContext* _pOutSeenInComplete;
        int32*          _pCompletedCount;
    };
} // namespace test

namespace test
{
    /** @brief 계약 케이스 몸들입니다(저장소 스레드). */
    struct ServiceStoreContract
    {
        /** @brief 일 하나를 맡기고 거둘 때까지 기다립니다(SQL 은 풀 워커 — 10 초 상한). 정확히 한 번 거뒀으면 true 입니다. */
        static bool executeWork( sw::IServiceStore& store, ServiceStoreContractWork::RunFunction pRun, ServiceStoreContractContext& context )
        {
            int32 completedCount = 0;
            store.submit( sw::make_unique<ServiceStoreContractWork>( pRun, &context, &completedCount ) );
            const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 10000 );
            while ( completedCount == 0 && deadline.isExpired() == false )
            {
                if ( store.pollCompletions() == 0 )
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            return completedCount == 1;
        }

        /**
         * @brief @p submitter 문맥 안에서 일 하나를 맡기고, 문맥 **밖에서** 거둡니다 — 저장소가 맡긴 쪽 문맥을 잡아 `run`(저장소 스레드) · `complete` 에
         *        다시 거는지 본다. 정확히 한 번 거뒀으면 true 입니다.
         */
        static bool executeLogContextWork( sw::IServiceStore& store, const sw::LogContext& submitter, sw::LogContext& outSeenInRun, sw::LogContext& outSeenInComplete )
        {
            int32 completedCount = 0;
            {
                sw::ScopedLogContext scope( submitter );
                store.submit( sw::make_unique<ServiceStoreLogContextWork>( &outSeenInRun, &outSeenInComplete, &completedCount ) );
            }
            const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 10000 );
            while ( completedCount == 0 && deadline.isExpired() == false )
            {
                if ( store.pollCompletions() == 0 )
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            return completedCount == 1;
        }

        static sw::vector<uint8> makeBytes( uint8 value ) { return sw::vector<uint8>{ value, static_cast<uint8>( value + 1 ) }; }

        static sw::string makeChannelKey( uint64 index )
        {
            sw::string key{ "chan/" };
            sw::ServiceKeyUtil::appendHex64( key, index );
            return key;
        }

        static void runConditionalPut( sw::IServiceStoreConnection& connection, ServiceStoreContractContext& context )
        {
            using namespace sw;
            ServiceTransaction create;
            create.put( context._table, "k", makeBytes( 1 ), ServiceRecord::kAbsentVersion );
            if ( context.recordStep( connection.commit( create ) == ServiceStoreResult::Ok ) == false )
                return;
            if ( context.recordStep( connection.commit( create ) == ServiceStoreResult::Conflict ) == false ) // 두 번째 만들기는 진다(고유 제약)
                return;
            ServiceRecord first;
            if ( context.recordStep( connection.readRecord( context._table, "k", first ) == ServiceStoreResult::Ok ) == false )
                return;
            ServiceTransaction erase;
            erase.erase( context._table, "k", first._version );
            if ( context.recordStep( connection.commit( erase ) == ServiceStoreResult::Ok ) == false )
                return;
            if ( context.recordStep( connection.commit( create ) == ServiceStoreResult::Ok ) == false )
                return;
            ServiceRecord second;
            if ( context.recordStep( connection.readRecord( context._table, "k", second ) == ServiceStoreResult::Ok ) == false )
                return;
            if ( context.recordStep( second._version > first._version ) == false ) // ABA 없음 — 다시 만든 키도 새 판
                return;
            ServiceTransaction stale;
            stale.put( context._table, "k", makeBytes( 9 ), first._version );
            if ( context.recordStep( connection.commit( stale ) == ServiceStoreResult::Conflict ) == false )
                return;
            ServiceRecord missing;
            if ( context.recordStep( connection.readRecord( context._table, "none", missing ) == ServiceStoreResult::NotFound ) == false )
                return;
            context.recordStep( missing._version == ServiceRecord::kAbsentVersion );
        }

        static void runAllOrNothing( sw::IServiceStoreConnection& connection, ServiceStoreContractContext& context )
        {
            using namespace sw;
            string otherName{ context._table.c_str() };
            otherName += "_b";
            const hashed_string other( otherName.c_str() );
            ServiceTransaction  seed;
            seed.put( context._table, "x", makeBytes( 1 ) );
            if ( context.recordStep( connection.commit( seed ) == ServiceStoreResult::Ok ) == false )
                return;
            ServiceTransaction mixed;
            mixed.put( other, "y", makeBytes( 2 ) );
            mixed.put( context._table, "x", makeBytes( 3 ), ServiceRecord::kAbsentVersion );
            ServiceCommitInfo info;
            if ( context.recordStep( connection.commit( mixed, &info ) == ServiceStoreResult::Conflict ) == false )
                return;
            if ( context.recordStep( info._conflictIndex == 1 || info._conflictIndex == -1 ) == false )
                return;
            ServiceRecord record;
            if ( context.recordStep( connection.readRecord( other, "y", record ) == ServiceStoreResult::NotFound ) == false ) // 앞의 쓰기도 되돌려졌다
                return;
            if ( context.recordStep( connection.readRecord( context._table, "x", record ) == ServiceStoreResult::Ok && record._bytes == makeBytes( 1 ) ) == false )
                return;
            ServiceTransaction duplicate;
            duplicate.put( other, "z", makeBytes( 4 ) );
            duplicate.erase( other, "z" );
            if ( context.recordStep( connection.commit( duplicate ) == ServiceStoreResult::Invalid ) == false )
                return;
            ServiceTransaction badKey;
            badKey.put( other, "Upper Case", makeBytes( 5 ) );
            context.recordStep( connection.commit( badKey ) == ServiceStoreResult::Invalid );
        }

        static void runRequireVersion( sw::IServiceStoreConnection& connection, ServiceStoreContractContext& context )
        {
            using namespace sw;
            ServiceTransaction seed;
            seed.put( context._table, "balance", makeBytes( 10 ) );
            if ( context.recordStep( connection.commit( seed ) == ServiceStoreResult::Ok ) == false )
                return;
            ServiceRecord read;
            if ( context.recordStep( connection.readRecord( context._table, "balance", read ) == ServiceStoreResult::Ok ) == false )
                return;
            ServiceTransaction other;
            other.put( context._table, "balance", makeBytes( 20 ) );
            if ( context.recordStep( connection.commit( other ) == ServiceStoreResult::Ok ) == false )
                return;
            ServiceTransaction dependent;
            dependent.requireVersion( context._table, "balance", read._version );
            dependent.put( context._table, "derived", makeBytes( 11 ) );
            if ( context.recordStep( connection.commit( dependent ) == ServiceStoreResult::Conflict ) == false )
                return;
            ServiceRecord derived;
            if ( context.recordStep( connection.readRecord( context._table, "derived", derived ) == ServiceStoreResult::NotFound ) == false )
                return;
            ServiceTransaction absent; // "없어야 한다" 조건만(쓰지 않음)
            absent.requireVersion( context._table, "ghost", ServiceRecord::kAbsentVersion );
            absent.put( context._table, "after_ghost", makeBytes( 12 ) );
            context.recordStep( connection.commit( absent ) == ServiceStoreResult::Ok );
        }

        static void runListRecords( sw::IServiceStoreConnection& connection, ServiceStoreContractContext& context )
        {
            using namespace sw;
            ServiceTransaction seed;
            for ( uint64 index = 0; index < 5; ++index )
            {
                seed.put( context._table, makeChannelKey( index ), makeBytes( static_cast<uint8>( index ) ) );
            }
            seed.put( context._table, "chao", makeBytes( 50 ) ); // 접두어 범위 바로 바깥
            seed.put( context._table, "chan0", makeBytes( 51 ) );
            seed.put( context._table, "cham", makeBytes( 52 ) );
            if ( context.recordStep( connection.commit( seed ) == ServiceStoreResult::Ok ) == false )
                return;
            vector<ServiceRecord> listRecord;
            if ( context.recordStep( connection.listRecords( context._table, "chan/", "", 3, false, listRecord ) == ServiceStoreResult::Ok ) == false )
                return;
            if ( context.recordStep( listRecord.size() == 3 ) == false )
                return;
            const bool bAscending = listRecord[0]._key == makeChannelKey( 0 ) && listRecord[2]._key == makeChannelKey( 2 ) && listRecord[1]._bytes == makeBytes( 1 );
            if ( context.recordStep( bAscending ) == false )
                return;
            const string cursor = listRecord[2]._key;
            listRecord.clear();
            if ( context.recordStep( connection.listRecords( context._table, "chan/", cursor, 10, false, listRecord ) == ServiceStoreResult::Ok ) == false )
                return;
            if ( context.recordStep( listRecord.size() == 2 ) == false )
                return;
            listRecord.clear();
            if ( context.recordStep( connection.listRecords( context._table, "chan/", "", 2, true, listRecord ) == ServiceStoreResult::Ok ) == false )
                return;
            if ( context.recordStep( listRecord.size() == 2 && listRecord[0]._key == makeChannelKey( 4 ) && listRecord[1]._key == makeChannelKey( 3 ) ) == false )
                return;
            listRecord.clear();
            const string cursorDescending = makeChannelKey( 3 );
            if ( context.recordStep( connection.listRecords( context._table, "chan/", cursorDescending, 10, true, listRecord ) == ServiceStoreResult::Ok ) == false )
                return;
            context.recordStep( listRecord.size() == 3 && listRecord[2]._key == makeChannelKey( 0 ) );
        }

        static void runIdempotency( sw::IServiceStoreConnection& connection, ServiceStoreContractContext& context )
        {
            using namespace sw;
            vector<uint8> replyBytes;
            if ( context.recordStep( ServiceIdempotency::findReply( connection, "acct", 1, 7, replyBytes ) == ServiceStoreResult::NotFound ) == false )
                return;
            ServiceTransaction effect;
            effect.put( context._table, "gold", makeBytes( 5 ) );
            ServiceIdempotency::addReply( effect, "acct", 1, 7, makeBytes( 42 ) );
            if ( context.recordStep( connection.commit( effect ) == ServiceStoreResult::Ok ) == false )
                return;
            ServiceTransaction again;
            again.put( context._table, "gold", makeBytes( 6 ) );
            ServiceIdempotency::addReply( again, "acct", 1, 7, makeBytes( 43 ) );
            if ( context.recordStep( connection.commit( again ) == ServiceStoreResult::Conflict ) == false ) // 같은 키의 두 번째 효과는 진다
                return;
            if ( context.recordStep( ServiceIdempotency::findReply( connection, "acct", 1, 7, replyBytes ) == ServiceStoreResult::Ok && replyBytes == makeBytes( 42 ) ) == false )
                return;
            ServiceRecord gold;
            if ( context.recordStep( connection.readRecord( context._table, "gold", gold ) == ServiceStoreResult::Ok && gold._bytes == makeBytes( 5 ) ) == false )
                return;
            context.recordStep( ServiceIdempotency::findReply( connection, "other", 1, 7, replyBytes ) == ServiceStoreResult::NotFound );
        }

        static void runBinaryValues( sw::IServiceStoreConnection& connection, ServiceStoreContractContext& context )
        {
            using namespace sw;
            vector<uint8> bytes( static_cast<size_t>( IServiceStoreConnection::kMaxRecordSize ) );
            for ( size_t index = 0; index < bytes.size(); ++index )
            {
                bytes[index] = static_cast<uint8>( index * 31u ); // 0x00 · 0xFF 모두 든다
            }
            ServiceTransaction big;
            big.put( context._table, "big", bytes );
            if ( context.recordStep( connection.commit( big ) == ServiceStoreResult::Ok ) == false )
                return;
            ServiceRecord record;
            if ( context.recordStep( connection.readRecord( context._table, "big", record ) == ServiceStoreResult::Ok && record._bytes == bytes ) == false )
                return;
            bytes.push_back( 0 );
            ServiceTransaction tooBig;
            tooBig.put( context._table, "toobig", bytes );
            if ( context.recordStep( connection.commit( tooBig ) == ServiceStoreResult::Invalid ) == false )
                return;
            ServiceTransaction empty;
            empty.put( context._table, "empty", vector<uint8>{} );
            if ( context.recordStep( connection.commit( empty ) == ServiceStoreResult::Ok ) == false )
                return;
            context.recordStep( connection.readRecord( context._table, "empty", record ) == ServiceStoreResult::Ok && record._bytes.empty() );
        }
    };
} // namespace test

/** @brief 계약 케이스 하나를 @p SuiteName 스위트에 만듭니다. 픽스처는 `IServiceStore& getStore()` · `hashed_string makeTableName( const utf8* )` 를 준다. */
#define SW_SERVICE_STORE_CONTRACT_CASE( SuiteName, FixtureType, CaseName, RunFunction )                                                     \
    SW_TEST_CASE( SuiteName, CaseName )                                                                                                     \
    {                                                                                                                                       \
        FixtureType                       fixture;                                                                                          \
        test::ServiceStoreContractContext context;                                                                                          \
        context._table = fixture.makeTableName( #CaseName );                                                                                \
        SW_ASSERT_TRUE( test::ServiceStoreContract::executeWork( fixture.getStore(), &test::ServiceStoreContract::RunFunction, context ) ); \
        SW_EXPECT_EQUAL( -1, context._failedStep );                                                                                         \
    }

/** @brief 로그 문맥 케이스 — 맡긴 스레드의 문맥(추적 id · 주체)이 저장소 스레드의 `run` 과 문맥 없는 스레드의 `complete` 에 그대로 걸린다. */
#define SW_SERVICE_STORE_CONTRACT_LOG_CONTEXT_CASE( SuiteName, FixtureType )                                                             \
    SW_TEST_CASE( SuiteName, WorkCarriesTheSubmittersLogContext )                                                                        \
    {                                                                                                                                    \
        FixtureType    fixture;                                                                                                          \
        sw::LogContext submitter;                                                                                                        \
        submitter._traceId     = sw::LogTraceId{ 0x11, 0x22 };                                                                           \
        submitter._principalId = 0x42;                                                                                                   \
        sw::LogContext seenInRun;                                                                                                        \
        sw::LogContext seenInComplete;                                                                                                   \
        SW_ASSERT_TRUE( test::ServiceStoreContract::executeLogContextWork( fixture.getStore(), submitter, seenInRun, seenInComplete ) ); \
        SW_EXPECT_TRUE( seenInRun._traceId == submitter._traceId );                                                                      \
        SW_EXPECT_EQUAL( seenInRun._principalId, uint64( 0x42 ) );                                                                       \
        SW_EXPECT_TRUE( seenInComplete._traceId == submitter._traceId );                                                                 \
        SW_EXPECT_EQUAL( seenInComplete._principalId, uint64( 0x42 ) );                                                                  \
        SW_EXPECT_TRUE( sw::LogContext::getCurrent().isEmpty() );                                                                        \
    }

/** @brief 계약 케이스 일곱을 @p SuiteName 스위트로 만듭니다. */
#define SW_SERVICE_STORE_CONTRACT_SUITE( SuiteName, FixtureType )                                                                \
    SW_SERVICE_STORE_CONTRACT_LOG_CONTEXT_CASE( SuiteName, FixtureType )                                                         \
    SW_SERVICE_STORE_CONTRACT_CASE( SuiteName, FixtureType, ConditionalPutCreatesOnceAndVersionsNeverRepeat, runConditionalPut ) \
    SW_SERVICE_STORE_CONTRACT_CASE( SuiteName, FixtureType, TransactionIsAllOrNothing, runAllOrNothing )                         \
    SW_SERVICE_STORE_CONTRACT_CASE( SuiteName, FixtureType, RequireVersionGuardsReadSet, runRequireVersion )                     \
    SW_SERVICE_STORE_CONTRACT_CASE( SuiteName, FixtureType, ListsByPrefixInBothDirections, runListRecords )                      \
    SW_SERVICE_STORE_CONTRACT_CASE( SuiteName, FixtureType, IdempotencyRecordBlocksASecondEffect, runIdempotency )               \
    SW_SERVICE_STORE_CONTRACT_CASE( SuiteName, FixtureType, BinaryValuesRoundTripUpToTheLimit, runBinaryValues )
