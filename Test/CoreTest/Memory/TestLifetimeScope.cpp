#include "pch.h"

#include "Core/Memory/LifetimeScope.h"

#include "TestFramework/TestFramework.h"

#include <thread>

using namespace sw;

// ------------------------------------------------------------------------------
// 1) LifetimeScopeTest — 지연 생성 · 닫으면 역순 소멸 · 닫힌 뒤 거절 · 먼저 소멸한 값의 해제 · 동시 생성
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 생성 · 소멸을 기록하는 값입니다. 소멸 순서를 `s_arrDestroyedID` 에 남깁니다. */
    struct LifetimeScopeTestValue
    {
        static constexpr uint32 kMaxRecord = 8;

        static inline std::atomic<uint32> s_createdCount{ 0 };
        static inline uint32              s_destroyedCount{ 0 };
        static inline uint32              s_arrDestroyedID[kMaxRecord]{};
        static inline uint32              s_nextID{ 0 };

        static void resetRecord()
        {
            s_createdCount   = 0;
            s_destroyedCount = 0;
            s_nextID         = 0;
        }

        LifetimeScopeTestValue()
            : _id{ s_nextID++ }
        {
            ++s_createdCount;
        }

        ~LifetimeScopeTestValue()
        {
            if ( s_destroyedCount < kMaxRecord )
                s_arrDestroyedID[s_destroyedCount] = _id;
            ++s_destroyedCount;
        }

        uint32 _id;
    };
} // namespace

/**
 * @brief [LifetimeScopeTest] 값은 처음 부를 때 한 번 만들어지고, `find` 는 만들지 않는다
 */
SW_TEST_CASE( LifetimeScopeTest, CreatesOnFirstUseOnly )
{
    LifetimeScopeTestValue::resetRecord();
    LifetimeScope                      scope;
    ScopeOwned<LifetimeScopeTestValue> owned{ &scope };

    SW_EXPECT_NULL( owned.find() );
    SW_EXPECT_EQUAL( 0u, LifetimeScopeTestValue::s_createdCount.load() );

    LifetimeScopeTestValue* const pFirst = owned.getOrCreate();
    SW_ASSERT_NOT_NULL( pFirst );
    SW_EXPECT_TRUE( owned.getOrCreate() == pFirst );
    SW_EXPECT_TRUE( owned.find() == pFirst );
    SW_EXPECT_EQUAL( 1u, LifetimeScopeTestValue::s_createdCount.load() );

    scope.close();
}

/**
 * @brief [LifetimeScopeTest] 닫은 범위를 다시 열면 같은 자리가 새 값을 만든다 — 한 프로세스에서 엔진을 다시 세우는 호스트(`StaticStorageSubsystem::initializeSubsystem`)
 */
SW_TEST_CASE( LifetimeScopeTest, ReopenedScopeCreatesAgain )
{
    LifetimeScopeTestValue::resetRecord();
    LifetimeScope                      scope;
    ScopeOwned<LifetimeScopeTestValue> owned{ &scope };
    SW_ASSERT_NOT_NULL( owned.getOrCreate() );
    scope.close();
    SW_EXPECT_NULL( owned.find() );

    scope.open();
    SW_EXPECT_FALSE( scope.isClosed() );
    SW_EXPECT_NOT_NULL( owned.getOrCreate() );
    SW_EXPECT_EQUAL( 2u, LifetimeScopeTestValue::s_createdCount.load() );
    scope.close();
}

/**
 * @brief [LifetimeScopeTest] 닫으면 만든 역순으로 소멸하고 자리가 빈다
 */
SW_TEST_CASE( LifetimeScopeTest, CloseDestroysInReverseCreationOrder )
{
    LifetimeScopeTestValue::resetRecord();
    LifetimeScope                      scope;
    ScopeOwned<LifetimeScopeTestValue> first{ &scope };
    ScopeOwned<LifetimeScopeTestValue> second{ &scope };
    ScopeOwned<LifetimeScopeTestValue> third{ &scope };

    // 선언 순서와 다르게 만든다 — 소멸은 선언이 아니라 생성의 역순이다.
    SW_ASSERT_NOT_NULL( second.getOrCreate() ); // id 0
    SW_ASSERT_NOT_NULL( third.getOrCreate() );  // id 1
    SW_ASSERT_NOT_NULL( first.getOrCreate() );  // id 2

    scope.close();

    SW_EXPECT_TRUE( scope.isClosed() );
    SW_ASSERT_EQUAL( 3u, LifetimeScopeTestValue::s_destroyedCount );
    SW_EXPECT_EQUAL( 2u, LifetimeScopeTestValue::s_arrDestroyedID[0] );
    SW_EXPECT_EQUAL( 1u, LifetimeScopeTestValue::s_arrDestroyedID[1] );
    SW_EXPECT_EQUAL( 0u, LifetimeScopeTestValue::s_arrDestroyedID[2] );
    SW_EXPECT_NULL( first.find() );
    SW_EXPECT_NULL( second.find() );
    SW_EXPECT_NULL( third.find() );
}

/**
 * @brief [LifetimeScopeTest] 닫힌 범위의 값은 만들지 않고 nullptr 이다(Debug 는 단언), 두 번 닫기도 단언이다
 */
SW_TEST_CASE( LifetimeScopeTest, RejectsCreationAndSecondCloseAfterClose )
{
    LifetimeScopeTestValue::resetRecord();
    LifetimeScope                      scope;
    ScopeOwned<LifetimeScopeTestValue> owned{ &scope };
    scope.close();

    test::ScopedAssertCapture capture;
    SW_EXPECT_NULL( owned.getOrCreate() );
    SW_EXPECT_EQUAL( 0u, LifetimeScopeTestValue::s_createdCount.load() );
    scope.close();
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 2u, capture.getCount() );
}

/**
 * @brief [LifetimeScopeTest] 범위보다 먼저 소멸한 자리(모듈 언로드)는 범위에서 빠지고, 뒤의 닫기는 그 값을 다시 건드리지 않는다
 */
SW_TEST_CASE( LifetimeScopeTest, ValueDestroyedBeforeCloseLeavesTheScope )
{
    LifetimeScopeTestValue::resetRecord();
    LifetimeScope                      scope;
    ScopeOwned<LifetimeScopeTestValue> survivor{ &scope };
    SW_ASSERT_NOT_NULL( survivor.getOrCreate() );
    {
        ScopeOwned<LifetimeScopeTestValue> unloaded{ &scope };
        SW_ASSERT_NOT_NULL( unloaded.getOrCreate() );
    }
    SW_EXPECT_EQUAL( 1u, LifetimeScopeTestValue::s_destroyedCount );

    scope.close();

    SW_EXPECT_EQUAL( 2u, LifetimeScopeTestValue::s_destroyedCount );
    SW_EXPECT_EQUAL( 0u, LifetimeScopeTestValue::s_arrDestroyedID[1] );
}

/**
 * @brief [LifetimeScopeTest] 여러 스레드가 동시에 처음 불러도 값은 하나만 만들어진다
 */
SW_TEST_CASE( LifetimeScopeTest, ConcurrentFirstUseCreatesOnce )
{
    static constexpr uint32 kThreadCount = 8;

    LifetimeScopeTestValue::resetRecord();
    LifetimeScope                      scope;
    ScopeOwned<LifetimeScopeTestValue> owned{ &scope };
    std::atomic<uint32>                readyCount{ 0 };
    LifetimeScopeTestValue*            arrSeen[kThreadCount]{};

    vector<std::thread> listWorker;
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listWorker.emplace_back( [&owned, &readyCount, &arrSeen, threadIndex]()
        {
            ++readyCount;
            while ( readyCount.load() < kThreadCount )
            {
                std::this_thread::yield();
            }
            arrSeen[threadIndex] = owned.getOrCreate();
        } );
    }
    for ( std::thread& worker : listWorker )
    {
        worker.join();
    }

    SW_EXPECT_EQUAL( 1u, LifetimeScopeTestValue::s_createdCount.load() );
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        SW_EXPECT_TRUE( arrSeen[threadIndex] == owned.find() );
    }
    scope.close();
}
