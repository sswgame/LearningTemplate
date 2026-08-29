#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/DeadlockDetector.h"
#include "Core/Concurrency/mutex.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) DeadlockDetectorTest — 기본 추적
// ------------------------------------------------------------------------------
/**
 * @brief [DeadlockDetectorTest] 잠금 의도·획득·해제 기록
 */
SW_TEST_CASE( DeadlockDetectorTest, BasicTracking )
{
	DeadlockDetector detector;
	detector.initialize();

	void* dummyLock = reinterpret_cast<void*>( 0x1234 );
	detector.recordLockIntended( dummyLock );
	detector.recordLockAcquired( dummyLock );
	detector.recordLockReleased( dummyLock );

	detector.shutdown();
}

/**
 * @brief [DeadlockDetectorTest] 다중 락 계층 획득 및 해제 시퀀스 검증
 */
SW_TEST_CASE( DeadlockDetectorTest, MultiLockHierarchicalAcquisitionAndRelease )
{
	DeadlockDetector detector;
	detector.initialize();

	void* lockA = reinterpret_cast<void*>( 0x1000 );
	void* lockB = reinterpret_cast<void*>( 0x2000 );
	void* lockC = reinterpret_cast<void*>( 0x3000 );

	// 스레드에서 A -> B -> C 순차 획득
	detector.recordLockIntended( lockA );
	detector.recordLockAcquired( lockA );

	detector.recordLockIntended( lockB );
	detector.recordLockAcquired( lockB );

	detector.recordLockIntended( lockC );
	detector.recordLockAcquired( lockC );

	// 역순 해제 C -> B -> A
	detector.recordLockReleased( lockC );
	detector.recordLockReleased( lockB );
	detector.recordLockReleased( lockA );

	detector.shutdown();
}
