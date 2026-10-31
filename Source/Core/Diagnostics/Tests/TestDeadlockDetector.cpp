// DeadlockDetector — 잠금 관찰자 자리를 빼앗지 않고(먼저 걸린 탐지기를 그대로 둔다) 떼면 되돌리며, 사이클 없는 계층 획득 · 역순 해제는 교착으로 보지 않는다.
#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Diagnostics/DeadlockDetector.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    // 케이스 밖 수명 — 걸린 관찰자는 다른 스레드가 포인터를 읽은 뒤 부를 수 있어(`mutex::notify*`), 케이스가 끝나도 프로세스 끝까지 산다.
    DeadlockDetector s_deadlockDetectorUnderTest;
} // namespace

/**
 * @brief 계층 순서(A → B → C) 획득과 역순 해제는 교착이 아니다 — 교착이면 탐지기가 오류 로그를 남기고 단언하므로 케이스가 진다.
 * @details 엔진의 진단 서브시스템이 탐지기를 먼저 걸었으면(진단을 켠 실행) 그것이 자리를 지키고, 아니면 이 탐지기가 걸린다. 어느 쪽이든 떼면 이전으로 돌아간다.
 */
SW_TEST_CASE( DeadlockDetectorTest, HierarchicalAcquisitionIsNotADeadlock )
{
    ILockObserver* const pBefore = mutex::getLockObserver();
    s_deadlockDetectorUnderTest.initialize();
    SW_EXPECT_TRUE( mutex::getLockObserver() == ( pBefore != nullptr ? pBefore : &s_deadlockDetectorUnderTest ) );

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
