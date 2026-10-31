// LockFreeObjectPool — 다른 블록이 나가 있는 동안의 이중 반납 거절, 수명 · 인자 전달, 바닥남과 재획득, 멀티스레드 획득 · 반납, 경합 속 블록 보존, 자기 블록 판정.
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Task/LockFreeObjectPool.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 살아 있는 개수를 세는 풀 원소 — 소멸자가 두 번 돌면 음수가 된다. */
    struct PoolLifeProbe
    {
        static inline int32 s_liveCount = 0;
        PoolLifeProbe() { ++s_liveCount; }
        ~PoolLifeProbe() { --s_liveCount; }
    };

    struct PooledTestStruct
    {
        int32      _id{ 0 };
        sw::string _message{};

        PooledTestStruct( int32 id, sw::string message )
            : _id{ id }
            , _message{ std::move( message ) }
        {
        }

        ~PooledTestStruct()
        {
            _id = -1;
        }
    };
} // namespace

/**
 * @brief [LockFreeObjectPoolTest] 다른 블록이 나가 있는 동안의 이중 반납도 잡는다 — 같은 블록이 두 번 나가지 않는다
 * @details 자유 큐가 가득 찼을 때만(다른 블록이 모두 돌아와 있을 때만) 알아채면, 다른 블록이 나가 있는 동안의 두 번째 반납이 큐에
 *          들어가 같은 블록이 자유 목록에 두 번 들고 소멸자도 두 번 돈다 — 그 뒤 두 `acquire` 가 같은 메모리를 받는다.
 *          Debug 의 `SW_LOG_ASSERT` 는 단언 가로채기 안에서 세어지고 그 뒤의 방어 경로가 돈다.
 */
SW_TEST_CASE( LockFreeObjectPoolTest, CatchesDoubleReleaseWhileOthersAreOut )
{
    SW_EXPECT_LOG( SW_ASSERT_LOG_ID );
    test::ScopedAssertCapture assertCapture;
    PoolLifeProbe::s_liveCount = 0;
    {
        sw::LockFreeObjectPool<PoolLifeProbe, 4> pool;
        PoolLifeProbe*                           pFirst  = pool.acquire();
        PoolLifeProbe*                           pSecond = pool.acquire();
        SW_REQUIRE_NOT_NULL( pFirst );
        SW_REQUIRE_NOT_NULL( pSecond );
        PoolLifeProbe* pAlias = pFirst;

        pool.release( pFirst );
        pool.release( pAlias ); // 이중 반납 — 단언 로그를 남기고 거절한다
        SW_EXPECT_NULL( pAlias );
        SW_EXPECT_EQUAL( 1u, pool.getActiveCount() );
        SW_EXPECT_EQUAL( 1, PoolLifeProbe::s_liveCount ); // 소멸자는 한 번만

        // 남은 셋은 모두 다른 블록이고, 넷째는 없다(같은 블록이 자유 목록에 두 번 들었다면 넷째가 나온다).
        PoolLifeProbe* arrTaken[4]{ pool.acquire(), pool.acquire(), pool.acquire(), pool.acquire() };
        SW_EXPECT_NOT_NULL( arrTaken[0] );
        SW_EXPECT_NOT_NULL( arrTaken[1] );
        SW_EXPECT_NOT_NULL( arrTaken[2] );
        SW_EXPECT_NULL( arrTaken[3] );
        SW_EXPECT_TRUE( arrTaken[0] != arrTaken[1] && arrTaken[1] != arrTaken[2] && arrTaken[0] != arrTaken[2] );
        SW_EXPECT_TRUE( arrTaken[0] != pSecond && arrTaken[1] != pSecond && arrTaken[2] != pSecond );

        for ( PoolLifeProbe*& pTaken : arrTaken )
        {
            pool.release( pTaken );
        }
        pool.release( pSecond );
    }
    SW_EXPECT_EQUAL( 0, PoolLifeProbe::s_liveCount );
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
}

/**
 * @brief [LockFreeObjectPoolTest] LockFreeObjectPool 기본 수명주기 및 인자 전달 검증
 */
SW_TEST_CASE( LockFreeObjectPoolTest, Lifecycle )
{
    sw::LockFreeObjectPool<PooledTestStruct, 16> pool;
    SW_EXPECT_EQUAL( 16u, pool.capacity() );
    SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
    SW_EXPECT_EQUAL( 16u, pool.getAvailableCount() );

    PooledTestStruct* obj1 = pool.acquire( 42, sw::string( "Hello Pool" ) );
    SW_REQUIRE_NOT_NULL( obj1 );
    SW_EXPECT_EQUAL( 42, obj1->_id );
    SW_EXPECT_EQUAL( sw::string( "Hello Pool" ), obj1->_message );
    SW_EXPECT_EQUAL( 1u, pool.getActiveCount() );
    SW_EXPECT_EQUAL( 15u, pool.getAvailableCount() );

    PooledTestStruct* obj2 = pool.acquire( 100, sw::string( "Second Object" ) );
    SW_REQUIRE_NOT_NULL( obj2 );
    SW_EXPECT_EQUAL( 2u, pool.getActiveCount() );
    SW_EXPECT_EQUAL( 14u, pool.getAvailableCount() );

    pool.release( obj1 );
    SW_EXPECT_NULL( obj1 );
    SW_EXPECT_EQUAL( 1u, pool.getActiveCount() );
    SW_EXPECT_EQUAL( 15u, pool.getAvailableCount() );

    pool.release( obj2 );
    SW_EXPECT_NULL( obj2 );
    SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
    SW_EXPECT_EQUAL( 16u, pool.getAvailableCount() );
}

/**
 * @brief [LockFreeObjectPoolTest] LockFreeObjectPool 용량 초과 및 반납 후 재획득 검증
 */
SW_TEST_CASE( LockFreeObjectPoolTest, ExhaustionAndReacquire )
{
    constexpr uint32                        kPoolCap = 4;
    sw::LockFreeObjectPool<int32, kPoolCap> pool;

    int32* items[kPoolCap]{};
    for ( uint32 itemIndex = 0; itemIndex < kPoolCap; ++itemIndex )
    {
        items[itemIndex] = pool.acquire( static_cast<int32>( itemIndex * 10 ) );
        SW_REQUIRE_NOT_NULL( items[itemIndex] );
        SW_EXPECT_EQUAL( static_cast<int32>( itemIndex * 10 ), *items[itemIndex] );
    }

    SW_EXPECT_EQUAL( kPoolCap, pool.getActiveCount() );
    SW_EXPECT_EQUAL( 0u, pool.getAvailableCount() );

    // 용량 초과 시 nullptr 반환 검증
    int32* overflow = pool.acquire( 999 );
    SW_EXPECT_NULL( overflow );

    // 하나 반납 후 즉시 재획득 가능 검증
    pool.release( items[1] );
    SW_EXPECT_NULL( items[1] );
    SW_EXPECT_EQUAL( kPoolCap - 1u, pool.getActiveCount() );
    SW_EXPECT_EQUAL( 1u, pool.getAvailableCount() );

    items[1] = pool.acquire( 777 );
    SW_REQUIRE_NOT_NULL( items[1] );
    SW_EXPECT_EQUAL( 777, *items[1] );
    SW_EXPECT_EQUAL( kPoolCap, pool.getActiveCount() );

    // 전체 반납
    for ( uint32 itemIndex = 0; itemIndex < kPoolCap; ++itemIndex )
    {
        pool.release( items[itemIndex] );
        SW_EXPECT_NULL( items[itemIndex] );
    }

    SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
    SW_EXPECT_EQUAL( kPoolCap, pool.getAvailableCount() );
}

/**
 * @brief [LockFreeObjectPoolTest] LockFreeObjectPool 멀티스레드 동시 acquire/release 검증
 */
SW_TEST_CASE( LockFreeObjectPoolTest, Concurrent )
{
    constexpr uint32 kCapacity   = 256;
    constexpr int32  kIterations = 1000;
    constexpr int32  kNumThreads = 4;

    sw::LockFreeObjectPool<int32, kCapacity> pool;
    sw::atomic<int32>                        successfulAcquires{ 0 };

    sw::vector<sw::thread> workers;
    for ( int32 threadIndex = 0; threadIndex < kNumThreads; ++threadIndex )
    {
        workers.emplace_back( [&pool, &successfulAcquires, threadIndex]()
        {
            for ( int32 iter = 0; iter < kIterations; ++iter )
            {
                int32* ptr = pool.acquire( threadIndex * 1000 + iter );
                if ( ptr != nullptr )
                {
                    SW_EXPECT_EQUAL( threadIndex * 1000 + iter, *ptr );
                    successfulAcquires.fetch_add( 1, std::memory_order_relaxed );
                    std::this_thread::yield();
                    pool.release( ptr );
                    SW_EXPECT_NULL( ptr );
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        } );
    }

    for ( sw::thread& t : workers )
    {
        t.join();
    }

    SW_EXPECT_TRUE( successfulAcquires.load() > 0 );
    SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
    SW_EXPECT_EQUAL( kCapacity, pool.getAvailableCount() );
}

/**
 * @brief [LockFreeObjectPoolTest] 경합 속에서 반납된 블록이 하나도 사라지지 않는다
 * @details 내부 MPMC 큐는 소비자가 칸을 집어간 뒤 **순번을 아직 공개하지 않은 찰나**에도
 *          생산자에게 "가득 찼다" 고 답한다. 반납이 그 답을 이중 반납으로 읽고 물러서면 그 블록은
 *          자유 목록으로 돌아가지 못한다 — 풀이 **조용히 줄어들고**, 오래 돌수록 `acquire` 가 더
 *          자주 널을 돌려준다. 자리 수와 블록 수가 같으므로 그 찰나는 반드시 지나가니, 반납은
 *          물러서지 말고 다시 시도해야 한다.
 *
 *          작은 풀에 코어보다 많은 스레드를 붙여 그 찰나를 자주 만든다. 끝에 남는 질문은 하나다 —
 *          **풀이 여전히 용량만큼 내줄 수 있는가.**
 */
SW_TEST_CASE( LockFreeObjectPoolTest, LosesNoBlockUnderContention )
{
    constexpr uint32 kCapacity   = 8;
    constexpr int32  kThreads    = 8;
    constexpr int32  kIterations = 20000;

    sw::LockFreeObjectPool<int32, kCapacity> pool;

    sw::vector<sw::thread> workers;
    for ( int32 threadIndex = 0; threadIndex < kThreads; ++threadIndex )
    {
        workers.emplace_back( [&pool]()
        {
            for ( int32 iter = 0; iter < kIterations; ++iter )
            {
                int32* pValue = pool.acquire( iter );
                if ( pValue != nullptr )
                    pool.release( pValue );
            }
        } );
    }
    for ( sw::thread& worker : workers )
    {
        worker.join();
    }

    SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );

    // **용량만큼 다시 내줄 수 있어야 한다.** 블록 하나라도 잃었으면 여기서 널이 나온다.
    sw::vector<int32*> listTaken;
    for ( uint32 takeIndex = 0; takeIndex < kCapacity; ++takeIndex )
    {
        int32* pValue = pool.acquire( static_cast<int32>( takeIndex ) );
        if ( pValue != nullptr )
            listTaken.push_back( pValue );
    }
    SW_EXPECT_TRUE_MSG( listTaken.size() == kCapacity,
                        "경합 중 반납된 블록이 사라졌습니다 — 풀이 용량보다 적게 내줍니다" );

    for ( int32*& pValue : listTaken )
    {
        pool.release( pValue );
    }
}

/**
 * @brief [LockFreeObjectPoolTest] 풀은 자기 블록만 자기 것이라고 말한다
 * @details `release` 가 남의 포인터를 **기다리지 않고** 가려내는 근거다. 남의 포인터를 "자리가 안 난다" 는
 *          증상으로 알아내면 그 증상이 경합 중인 **정상 반납**과 구별되지 않아, 코어가 적은 CI 에서 진짜 반납이
 *          그 단언에 걸려 프로세스가 죽는다. 범위와 간격은 기다릴 필요 없이 확정된다.
 */
SW_TEST_CASE( LockFreeObjectPoolTest, OwnsOnlyItsOwnBlocks )
{
    constexpr uint32 kCapacity = 4;

    sw::LockFreeObjectPool<int64, kCapacity> pool;
    sw::LockFreeObjectPool<int64, kCapacity> other;

    SW_EXPECT_FALSE( pool.owns( nullptr ) );

    int64 stackValue{ 7 };
    SW_EXPECT_FALSE_MSG( pool.owns( &stackValue ), "스택 변수를 자기 블록이라고 말했다" );

    int64* pMine = pool.acquire( 1 );
    SW_REQUIRE_NOT_NULL( pMine );
    SW_EXPECT_TRUE_MSG( pool.owns( pMine ), "자기가 내준 블록을 자기 것이 아니라고 말했다" );

    int64* pTheirs = other.acquire( 2 );
    SW_REQUIRE_NOT_NULL( pTheirs );
    SW_EXPECT_FALSE_MSG( pool.owns( pTheirs ), "다른 풀의 블록을 자기 것이라고 말했다" );
    SW_EXPECT_FALSE_MSG( other.owns( pMine ), "다른 풀의 블록을 자기 것이라고 말했다" );

    // 블록 **중간**을 가리키는 포인터는 이 풀의 것이 아니다 — 범위만 보고 간격을 안 보면 통과한다.
    const int64* pInterior = reinterpret_cast<const int64*>( reinterpret_cast<const uint8*>( pMine ) + 1 );
    SW_EXPECT_FALSE_MSG( pool.owns( pInterior ), "블록 중간을 가리키는 포인터를 자기 블록이라고 말했다" );

    pool.release( pMine );
    other.release( pTheirs );
}
