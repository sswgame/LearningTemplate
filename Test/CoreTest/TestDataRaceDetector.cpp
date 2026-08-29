#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "TestFramework/TestFramework.h"

using namespace std::chrono_literals;

// ------------------------------------------------------------------------------
// 1) Core — 데이터 레이스 감지
// ------------------------------------------------------------------------------
/**
 * @brief [Core] 단일 스레드 접근은 레이스가 없다
 */
SW_TEST_CASE( Core, DataRaceDetector_SingleThreadSafe )
{
	sw::vector<int32> vec;

	for ( int32 iteration = 0; iteration < 1000; ++iteration )
	{
		vec.push_back( iteration );
	}

	SW_ASSERT_TRUE( vec.size() == 1000 );
}

/**
 * @brief [Core] 다중 스레드 읽기-읽기는 레이스가 없다
 */
SW_TEST_CASE( Core, DataRaceDetector_ReadReadSafe )
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
 * @brief [Core] RaceDetectContext 와 ScopedRaceRead / ScopedRaceWrite RAII 생명주기 검증
 */
SW_TEST_CASE( Core, DataRaceDetector_ScopedRAIILifecycle )
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
