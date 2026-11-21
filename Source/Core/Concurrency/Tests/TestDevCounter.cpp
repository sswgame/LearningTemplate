// DevCounterTest — 개발 구성 전용 누적 카운터가 더하고 되돌린다(배포는 빈 형식이라 테스트가 지어지지 않는다).
#include "pch.h"

#include "Core/Concurrency/DevCounter.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [DevCounterTest] 기본 1 과 준 수를 더하고, 복사는 센 수를 옮기고, `reset` 이 0 으로 되돌린다
 * @details 변이: `add` 가 인자를 무시하거나 `reset` 이 비우지 않으면 단언이 진다.
 */
SW_TEST_CASE( DevCounterTest, AddsAndResets )
{
    DevCounter counter;
    SW_EXPECT_EQUAL( uint64{ 0 }, counter.get() );
    counter.add();
    counter.add( 4 );
    SW_EXPECT_EQUAL( uint64{ 5 }, counter.get() );
    const DevCounter copied{ counter }; // 칸을 든 형식의 복사 · 이동은 센 수를 옮긴다
    SW_EXPECT_EQUAL( uint64{ 5 }, copied.get() );
    counter.reset();
    SW_EXPECT_EQUAL( uint64{ 0 }, counter.get() );
}
