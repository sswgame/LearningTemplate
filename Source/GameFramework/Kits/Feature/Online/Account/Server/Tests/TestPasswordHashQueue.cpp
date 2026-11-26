// PasswordHashQueueTest — 비밀번호 해시 줄(감사 O-3): 티켓이 (돌기 + 기다리기) 상한을 지키고 쓰지 않은 티켓은 자리를 돌려준다, 종료는 도는 해시를 끝내고 기다리던 해시를 취소로 돌려준다,
// 상한이 빠지면 기동 오류다. 해시는 진짜 작업 풀(`TaskManager`)에서 돈다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Task/TaskManager.h"
#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/PasswordHashQueue.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

using namespace sw;

namespace
{
    struct PasswordHashQueueTestInternal
    {
        static constexpr int64 kWaitMs = 10000; ///< 해시를 기다리는 상한(실패해도 멈추지 않게)

        /** @brief 비밀번호 글자 수를 해시 첫 바이트에 적는 가짜 해시입니다. 부른 수를 셉니다(워커가 부른다). */
        class CountingHasher final : public IPasswordHasher
        {
        public:
            atomic<int32> _callCount{ 0 };

            bool computePasswordHash( string_view password, const uint8* /*pSalt*/, int32 /*saltSize*/, const NetPasswordHashParams& /*params*/, uint8* pOut, int32 outSize ) override
            {
                _callCount.fetch_add( 1, std::memory_order_relaxed );
                for ( int32 index = 0; index < outSize; ++index )
                {
                    pOut[index] = static_cast<uint8>( password.size() );
                }
                return true;
            }
        };

        static PasswordHashRequest makeRequest( string_view password )
        {
            PasswordHashRequest request;
            request._password  = string( password );
            request._specCount = 1;
            return request;
        }

        static PasswordHashQueueConfig makeLimits( int32 concurrentCount, int32 queuedCount )
        {
            PasswordHashQueueConfig settings;
            settings._maxConcurrentPasswordHashes = concurrentCount;
            settings._maxQueuedPasswordHashes     = queuedCount;
            return settings;
        }
    };
} // namespace

/** @brief [PasswordHashQueueTest] 자리는 (돌기 + 기다리기) 상한까지만 나오고, 쓰지 않은 티켓은 사라지며 자리를 돌려준다. 맡긴 해시는 끝나면 자리를 돌려준다 */
SW_TEST_CASE( PasswordHashQueueTest, TicketBoundsInFlightAndQueued )
{
    using Internal            = PasswordHashQueueTestInternal;
    TaskManager* pTaskManager = game::findSubsystem<TaskManager>();
    SW_REQUIRE_NOT_NULL( pTaskManager );
    Internal::CountingHasher hasher;
    PasswordHashQueue        queue;
    SW_REQUIRE_TRUE( queue.initialize( *pTaskManager, hasher, Internal::makeLimits( 1, 1 ) ).has_value() );

    optional<PasswordHashTicket> first  = queue.tryReserve();
    optional<PasswordHashTicket> second = queue.tryReserve();
    SW_REQUIRE_TRUE( first.has_value() && second.has_value() );
    SW_EXPECT_FALSE( queue.tryReserve().has_value() ); // 돌기 1 + 기다리기 1 이 찼다
    SW_EXPECT_EQUAL( 2, queue.getOpenCount() );
    second.reset(); // 쓰지 않은 티켓은 자리를 돌려준다
    SW_EXPECT_EQUAL( 1, queue.getOpenCount() );
    optional<PasswordHashTicket> third = queue.tryReserve();
    SW_REQUIRE_TRUE( third.has_value() );

    const uint64 firstJob = queue.submit( std::move( *first ), Internal::makeRequest( "abc" ) );
    const uint64 thirdJob = queue.submit( std::move( *third ), Internal::makeRequest( "abcdef" ) );
    first.reset();
    third.reset();
    SW_EXPECT_EQUAL( 1, queue.getRunningCount() ); // 동시 상한 1 — 둘째는 기다린다
    SW_EXPECT_FALSE( queue.tryReserve().has_value() );

    vector<PasswordHashCompletion> listCompletion;
    for ( int32 round = 0; round < 4 && listCompletion.size() < 2; ++round )
    {
        SW_EXPECT_TRUE( queue.waitForRunning( Internal::kWaitMs ) );
        (void)queue.pollCompletions( listCompletion ); // 끝난 것을 거두고 기다리던 것을 올린다
    }
    SW_REQUIRE_EQUAL( size_t( 2 ), listCompletion.size() );
    SW_EXPECT_EQUAL( firstJob, listCompletion[0]._jobID );
    SW_EXPECT_EQUAL( thirdJob, listCompletion[1]._jobID );
    SW_EXPECT_TRUE( listCompletion[0]._bComputed == SW_TRUE && listCompletion[1]._bComputed == SW_TRUE );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listCompletion[0]._arrHash[0][0] ) );
    SW_EXPECT_EQUAL( 6, static_cast<int32>( listCompletion[1]._arrHash[0][0] ) );
    SW_EXPECT_EQUAL( 0, queue.getOpenCount() );
    SW_EXPECT_EQUAL( 2, hasher._callCount.load() );
}

/** @brief [PasswordHashQueueTest] 종료는 도는 해시를 끝까지 기다려 결과를 주고, 기다리던 해시는 돌리지 않고 취소로 돌려준다 */
SW_TEST_CASE( PasswordHashQueueTest, ShutdownFinishesRunningAndCancelsQueued )
{
    using Internal            = PasswordHashQueueTestInternal;
    TaskManager* pTaskManager = game::findSubsystem<TaskManager>();
    SW_REQUIRE_NOT_NULL( pTaskManager );
    Internal::CountingHasher hasher;
    PasswordHashQueue        queue;
    SW_REQUIRE_TRUE( queue.initialize( *pTaskManager, hasher, Internal::makeLimits( 1, 1 ) ).has_value() );
    optional<PasswordHashTicket> running = queue.tryReserve();
    optional<PasswordHashTicket> waiting = queue.tryReserve();
    SW_REQUIRE_TRUE( running.has_value() && waiting.has_value() );
    const uint64 runningJob = queue.submit( std::move( *running ), Internal::makeRequest( "run" ) );
    const uint64 waitingJob = queue.submit( std::move( *waiting ), Internal::makeRequest( "wait" ) );
    running.reset();
    waiting.reset();

    vector<PasswordHashCompletion> listCompletion;
    queue.shutdown( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 2 ), listCompletion.size() );
    SW_EXPECT_EQUAL( runningJob, listCompletion[0]._jobID );
    SW_EXPECT_TRUE( listCompletion[0]._bComputed == SW_TRUE );
    SW_EXPECT_EQUAL( waitingJob, listCompletion[1]._jobID );
    SW_EXPECT_TRUE( listCompletion[1]._bCancelled == SW_TRUE && listCompletion[1]._bComputed == SW_FALSE );
    SW_EXPECT_EQUAL( 1, hasher._callCount.load() ); // 기다리던 해시는 돌지 않았다
    SW_EXPECT_EQUAL( 0, queue.getOpenCount() );
}

/** @brief [PasswordHashQueueTest] 상한(동시 · 대기)이 1 미만이면 기동 오류다 — 기본값으로 대신하지 않는다 */
SW_TEST_CASE( PasswordHashQueueTest, MissingLimitIsAStartupError )
{
    using Internal            = PasswordHashQueueTestInternal;
    TaskManager* pTaskManager = game::findSubsystem<TaskManager>();
    SW_REQUIRE_NOT_NULL( pTaskManager );
    Internal::CountingHasher hasher;
    PasswordHashQueue        noConcurrent;
    PasswordHashQueue        noQueued;
    SW_EXPECT_FALSE( noConcurrent.initialize( *pTaskManager, hasher, Internal::makeLimits( 0, 4 ) ).has_value() );
    SW_EXPECT_FALSE( noQueued.initialize( *pTaskManager, hasher, Internal::makeLimits( 2, 0 ) ).has_value() );
    SW_EXPECT_FALSE( noQueued.tryReserve().has_value() ); // 서지 않은 줄은 자리를 내지 않는다
}
