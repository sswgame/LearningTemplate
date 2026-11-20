// RaceDetectContext — 읽기끼리 · 같은 스레드의 재진입(컨테이너의 가드 메서드가 가드 메서드를 부름)은 경합이 아니고, 주인이 나가면 다음 스레드가 조용히 들어오며, 복사본은 0 에서 시작하는지.
// 진짜 경합(다른 스레드가 동시에 들어옴)은 여기서 볼 수 없다 — 보고 뒤 디버거로 멈추기(`SW_DEBUG_BREAK`) 때문이다. Release 는 추적이 없어 모두 그냥 지난다.
#include "pch.h"

#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/flat_map.h"
#include "Core/Container/flat_set.h"
#include "Core/Container/string.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

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
    thread first( ThreadTestAccess::getKey(), readMany );
    thread second( ThreadTestAccess::getKey(), readMany );
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
    flat_set<int32> values;
    values = { 5, 17, 42 };
    SW_EXPECT_EQUAL( size_t( 3 ), values.size() );

    flat_map<string, int32> scores;
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
    thread         other( ThreadTestAccess::getKey(), [&context, &otherThreadDone]()
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
    thread         other( ThreadTestAccess::getKey(), [&copied, &otherThreadDone]()
            {
        copied.enterWrite();
        copied.exitWrite();
        otherThreadDone.store( 1 );
    } );
    other.join();
    original.exitWrite();
    SW_EXPECT_EQUAL( 1u, otherThreadDone.load() );
}

namespace
{
    /** @brief 테스트용 보고 함수입니다(불리지 않는다 — 칸의 주인만 본다). */
    void ignoreDataRaceReportForTest( const utf8*, const void*, uint32, uint32 )
    {
    }
    /** @brief 둘째 보고 함수입니다. */
    void ignoreDataRaceReportForTestOther( const utf8*, const void*, uint32, uint32 )
    {
    }
} // namespace

/**
 * @brief [DataRaceDetectorTest] 경합 보고 함수 칸은 하나다 — 엔진 보고기를 떼어 둔 범위에서 건 뒤 둘째 등록은 단언하고 덮지 않으며, 범위가 끝나면 엔진 보고기가 돌아온다
 */
SW_TEST_CASE( DataRaceDetectorTest, SecondReportFunctionBindAsserts )
{
    const sw::DataRaceReportFunction pHostReport = sw::RaceDetectContext::getReportFunction();
    {
        const sw::RaceDetectContext::ReportFunctionSuspension hostSuspension = sw::RaceDetectContext::suspendReportFunction();
        SW_EXPECT_TRUE( sw::RaceDetectContext::getReportFunction() == nullptr );
        sw::RaceDetectContext::ReportFunctionRegistration registration = sw::RaceDetectContext::bindReportFunction( &ignoreDataRaceReportForTest );
        SW_EXPECT_TRUE( registration.isActive() );
        {
            sw::test::ScopedAssertCapture capture;
            SW_EXPECT_LOG( SW_ASSERT_LOG_ID ); // 이중 등록 단언은 이 테스트가 보려는 대상이다
            const sw::RaceDetectContext::ReportFunctionRegistration second = sw::RaceDetectContext::bindReportFunction( &ignoreDataRaceReportForTestOther );
            SW_EXPECT_FALSE( second.isActive() );
            if ( sw::test::ScopedAssertCapture::kAssertsAreActive )
                SW_EXPECT_EQUAL( 1u, capture.getCount() );
        }
        SW_EXPECT_TRUE( sw::RaceDetectContext::getReportFunction() == &ignoreDataRaceReportForTest );
        registration.reset();
    }
    SW_EXPECT_TRUE( sw::RaceDetectContext::getReportFunction() == pHostReport );
}
