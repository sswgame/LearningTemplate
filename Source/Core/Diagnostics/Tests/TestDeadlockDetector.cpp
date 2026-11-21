// DeadlockDetector — 잠금 관찰자 자리는 하나라 둘째 등록은 단언이고(엔진 검출기는 범위 동안 떼어 둔다), 떼면 되돌리며, 사이클 없는 계층 획득 · 역순 해제는 교착으로 보지 않는다.
#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Diagnostics/DeadlockDetector.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    // 케이스 밖 수명 — 걸린 관찰자는 다른 스레드가 포인터를 읽은 뒤 부를 수 있어(`mutex::notify*`), 케이스가 끝나도 프로세스 끝까지 산다.
    DeadlockDetector s_deadlockDetectorUnderTest;
} // namespace

/**
 * @brief 계층 순서(A → B → C) 획득과 역순 해제는 교착이 아니다 — 교착이면 탐지기가 오류 로그를 남기고 단언하므로 케이스가 진다.
 * @details 엔진의 진단 서브시스템이 탐지기를 먼저 걸었으면(진단을 켠 실행) 범위 동안 떼어 두고 이 탐지기를 건다 — 관찰자는 하나라 그대로 걸면 이중 등록 단언이다.
 *          떼면 칸이 비고, 범위가 끝나면 엔진 검출기가 돌아온다.
 */
SW_TEST_CASE( DeadlockDetectorTest, HierarchicalAcquisitionIsNotADeadlock )
{
    const mutex::LockObserverSuspension hostSuspension = mutex::suspendLockObserver();
    ILockObserver* const                pBefore        = mutex::getLockObserver();
    SW_EXPECT_NULL( pBefore );
    s_deadlockDetectorUnderTest.initialize();
    SW_EXPECT_TRUE( mutex::getLockObserver() == &s_deadlockDetectorUnderTest );

    int32 arrLock[3]{};
    for ( int32& lock : arrLock )
    {
        s_deadlockDetectorUnderTest.recordLockAttempt( &lock );
        s_deadlockDetectorUnderTest.recordLockAcquired( &lock );
    }
    for ( int32 index = 2; index >= 0; --index )
        s_deadlockDetectorUnderTest.recordLockReleased( &arrLock[index] );

    s_deadlockDetectorUnderTest.shutdown();
    SW_EXPECT_TRUE( mutex::getLockObserver() == pBefore );
}

/**
 * @brief 잠금 관찰자는 하나다 — 걸린 동안 둘째 등록은 단언하고 앞 관찰자를 덮지 않는다
 */
SW_TEST_CASE( DeadlockDetectorTest, SecondLockObserverBindAsserts )
{
    const mutex::LockObserverSuspension hostSuspension = mutex::suspendLockObserver();
    s_deadlockDetectorUnderTest.initialize();
    {
        test::ScopedAssertCapture capture;
        SW_EXPECT_LOG( SW_ASSERT_LOG_ID ); // 이중 등록 단언은 이 테스트가 보려는 대상이다
        DeadlockDetector second;
        second.initialize();
        SW_EXPECT_TRUE( mutex::getLockObserver() == &s_deadlockDetectorUnderTest );
        if ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 1u, capture.getCount() );
        second.shutdown(); // 빈 토큰이라 앞 관찰자를 떼지 않는다
        SW_EXPECT_TRUE( mutex::getLockObserver() == &s_deadlockDetectorUnderTest );
    }
    s_deadlockDetectorUnderTest.shutdown();
    SW_EXPECT_NULL( mutex::getLockObserver() );
}
