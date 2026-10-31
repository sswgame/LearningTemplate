// RaceDetectContext — 읽기끼리 · 같은 스레드의 재진입(컨테이너의 가드 메서드가 가드 메서드를 부름)은 경합이 아니고, 주인이 나가면 다음 스레드가 조용히 들어오며, 복사본은 0 에서 시작하는지.
// 진짜 경합(다른 스레드가 동시에 들어옴)은 여기서 볼 수 없다 — 보고 뒤 디버거로 멈추기(`SW_DEBUG_BREAK`) 때문이다. Release 는 추적이 없어 모두 그냥 지난다.
#include "pch.h"

#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/map.h"
#include "Core/Container/set.h"
#include "Core/Container/string.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief 두 스레드가 같은 컨텍스트에 읽기로만 겹쳐 들어와도 경합이 아니다.
 */
SW_TEST_CASE( DataRaceDetectorTest, ConcurrentReadsAreNotARace )
{
    RaceDetectContext context;
    atomic<uint32>    finishedCount{ 0 };
    const auto        readMany = [&context, &finishedCount]()
    {
        for ( int32 iteration = 0; iteration < 1000; ++iteration )
        {
            const ScopedRaceRead read( context );
        }
        finishedCount.fetch_add( 1 );
    };
    thread first( readMany );
    thread second( readMany );
    first.join();
    second.join();
    SW_EXPECT_EQUAL( 2u, finishedCount.load() );
}

/**
 * @brief 같은 스레드의 재진입은 경합이 아니다 — `set::operator=( initializer_list )` 는 쓰기 가드를 잡고 `clear()` · `insert()` 를 부른다.
 * @details 검출기가 주인 스레드를 기억하지 않으면 Debug 에서 set · map 에 초기화 리스트를 대입하기만 해도 없는 경합이 뜬다(여기서 멈춘다).
 */
SW_TEST_CASE( DataRaceDetectorTest, SameThreadReentryIsNotARace )
{
    set<int32> values;
    values = { 5, 17, 42 };
    SW_EXPECT_EQUAL( size_t( 3 ), values.size() );

    map<string, int32> scores;
    scores = {
        { string( "hp" ), 100 }
    };
    SW_EXPECT_EQUAL( size_t( 1 ), scores.size() );

    // 쓰기 가드 안에서 읽기 가드를 부르는 경로도 같은 이유로 안전해야 한다.
    scores[string( "mp" )] = 50;
    SW_EXPECT_EQUAL( size_t( 2 ), scores.size() );
}

/**
 * @brief 주인이 나가면(수가 0) 주인 자리를 비워 다음 스레드가 조용히 들어온다 — 옛 주인 id 가 남으면 검출기가 눈이 먼다.
 */
SW_TEST_CASE( DataRaceDetectorTest, OwnerIsReleasedSoAnotherThreadCanEnter )
{
    RaceDetectContext context;
    context.enterWrite();
    context.exitWrite();

    atomic<uint32> otherThreadDone{ 0 };
    thread         other( [&context, &otherThreadDone]()
    {
        context.enterWrite();
        context.exitWrite();
        context.enterRead();
        context.exitRead();
        otherThreadDone.store( 1 );
    } );
    other.join();
    SW_EXPECT_EQUAL( 1u, otherThreadDone.load() );

    // 다시 이 스레드가 들어갈 수 있다(주인 자리가 또 비워졌다).
    context.enterRead();
    context.exitRead();
}

/**
 * @brief 컨텍스트를 복사하면 복사본은 0 에서 시작한다 — 원본이 쓰기 중이어도 다른 스레드가 복사본에 쓰기로 들어오면 경합이 아니다.
 */
SW_TEST_CASE( DataRaceDetectorTest, CopyStartsUnowned )
{
    RaceDetectContext original;
    original.enterWrite();
    RaceDetectContext copied = original;

    atomic<uint32> otherThreadDone{ 0 };
    thread         other( [&copied, &otherThreadDone]()
    {
        copied.enterWrite();
        copied.exitWrite();
        otherThreadDone.store( 1 );
    } );
    other.join();
    original.exitWrite();
    SW_EXPECT_EQUAL( 1u, otherThreadDone.load() );
}
