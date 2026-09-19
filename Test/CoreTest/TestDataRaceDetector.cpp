#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/map.h"
#include "Core/Container/set.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "TestFramework/TestFramework.h"

using namespace std::chrono_literals;

// ------------------------------------------------------------------------------
// 1) Core — 데이터 레이스 감지
// ------------------------------------------------------------------------------
/**
 * @brief [DataRaceDetectorTest] 단일 스레드 접근은 레이스가 없다
 */
SW_TEST_CASE( DataRaceDetectorTest, DataRaceDetector_SingleThreadSafe )
{
    sw::vector<int32> vec;

    for ( int32 iteration = 0; iteration < 1000; ++iteration )
    {
        vec.push_back( iteration );
    }

    SW_ASSERT_TRUE( vec.size() == 1000 );
}

/**
 * @brief [DataRaceDetectorTest] 다중 스레드 읽기-읽기는 레이스가 없다
 */
SW_TEST_CASE( DataRaceDetectorTest, DataRaceDetector_ReadReadSafe )
{
    sw::string str = "Hello, Data Race Detector!";

    std::thread t1( [&]()
    {
        for ( int32 iteration = 0; iteration < 1000; ++iteration )
        {
            volatile size_t sz = str.size();
            (void)sz;
        }
    } );

    std::thread t2( [&]()
    {
        for ( int32 iteration = 0; iteration < 1000; ++iteration )
        {
            volatile size_t sz = str.size();
            (void)sz;
        }
    } );

    t1.join();
    t2.join();

    // 읽기-읽기는 레이스 조건을 발생시키지 않아야 합니다.
    SW_ASSERT_TRUE( str.size() > 0 );
}

/**
 * @brief [DataRaceDetectorTest] RaceDetectContext 와 ScopedRaceRead / ScopedRaceWrite RAII 생명주기 검증
 */
SW_TEST_CASE( DataRaceDetectorTest, DataRaceDetector_ScopedRAIILifecycle )
{
    sw::RaceDetectContext ctx;

    // 순차 읽기 스코프
    {
        sw::ScopedRaceRead r1( ctx );
        {
            sw::ScopedRaceRead r2( ctx );
        }
    }

    // 순차 쓰기 스코프
    {
        sw::ScopedRaceWrite w1( ctx );
    }

    // 복사/이동 시 독립된 컨텍스트 상태 유지
    sw::RaceDetectContext copyCtx = ctx;
    sw::RaceDetectContext moveCtx = std::move( ctx );
    (void)copyCtx;
    (void)moveCtx;
}

/**
 * @brief [DataRaceDetectorTest] **같은 스레드의 재진입**은 레이스가 아니다
 * @details 데이터 레이스는 정의상 두 스레드가 필요하다. 그런데 검출기에 스레드 개념이 없던 시절에는
 *          한 스레드가 가드를 잡은 채 같은 객체의 다른 가드 메서드를 부르기만 해도 레이스로 보고했다 —
 *          `sw::set::operator=(initializer_list)` 가 쓰기 가드를 잡고 `clear()`·`insert()` 를 부르는
 *          것이 그 경우다. 즉 **Debug 빌드에서 set·map 에 초기화 리스트를 대입하기만 해도** 없는
 *          레이스가 떴다. "가드 메서드가 가드 메서드를 부르지 않게 조심한다" 로는 막히지 않는
 *          구조적 오탐이라, 검출기가 주인 스레드를 기억하게 했다.
 * @note 이 케이스가 실패하면(=레이스가 보고되면) 테스트 프레임워크가 그 자리에서 죽는다.
 */
SW_TEST_CASE( DataRaceDetectorTest, SameThreadReentryIsNotARace )
{
    // 대입 한 줄이 내부에서 clear() + insert() 를 부른다 — 예전에는 여기서 터졌다.
    sw::set<int32> values;
    values = { 5, 17, 42 };
    SW_EXPECT_EQUAL( size_t( 3 ), values.size() );

    sw::map<sw::string, int32> scores;
    scores = {
        { sw::string( "hp" ), 100 }
    };
    SW_EXPECT_EQUAL( size_t( 1 ), scores.size() );

    // 쓰기 가드 안에서 읽기 가드를 부르는 경로도 같은 이유로 안전해야 한다.
    scores[sw::string( "mp" )] = 50;
    SW_EXPECT_EQUAL( size_t( 2 ), scores.size() );
}

/**
 * @brief [DataRaceDetectorTest] 주인이 나가면 **다음 스레드가 조용히 들어온다**
 * @details 재진입 허용의 대가는 "주인을 기억한다" 는 것이고, 그 기억을 **제때 놓지 않으면** 검출기가
 *          눈이 먼다 — 옛 주인 id 가 남아 있는데 우연히 같은 스레드가 다시 들어오면, 그 사이 다른
 *          스레드가 들고 있어도 조용히 지나간다. 그래서 카운트가 0 이 되는 순간 주인을 비운다.
 *          이 케이스가 그 비우기를 지킨다.
 * @note 진짜 레이스(다른 스레드가 **동시에** 들어오는 것)는 여기서 검증할 수 없다 — 보고가 Fatal 이라
 *       프로세스가 그 자리에서 죽기 때문이다. 그 경로는 손으로 한 번 확인했다(2026-09-19: 다른
 *       스레드의 enterWrite 가 "Concurrent write/write access detected" 를 그대로 냈다).
 */
SW_TEST_CASE( DataRaceDetectorTest, OwnerIsReleasedSoAnotherThreadCanEnter )
{
    sw::RaceDetectContext context;

    // 이 스레드가 주인이 됐다가 나간다.
    context.enterWrite();
    context.exitWrite();

    // 주인이 비었으므로 다른 스레드가 들어와도 보고가 없어야 한다 — 보고되면 Fatal 로 여기서 죽는다.
    bool        bOtherThreadDone = false;
    std::thread other( [&context, &bOtherThreadDone]()
    {
        context.enterWrite();
        context.exitWrite();

        context.enterRead();
        context.exitRead();
        bOtherThreadDone = true;
    } );
    other.join();

    SW_EXPECT_TRUE( bOtherThreadDone );

    // 다시 이 스레드가 들어갈 수 있다(주인 자리가 또 비워졌다).
    context.enterRead();
    context.exitRead();
}
