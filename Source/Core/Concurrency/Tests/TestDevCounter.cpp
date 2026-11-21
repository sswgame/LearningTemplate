// DevCounterTest — 개발 구성 전용 누적 카운터가 더하고 되돌린다(배포는 빈 형식이라 테스트가 지어지지 않는다).
#include "pch.h"

#include "Core/Concurrency/DevCounter.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [DevCounterTest] 기본 1 과 준 수를 더하고 `reset` 이 0 으로 되돌린다
 * @details 변이: `add` 가 인자를 무시하거나 `reset` 이 비우지 않으면 단언이 진다.
 */
SW_TEST_CASE( DevCounterTest, AddsAndResets )
{
    DevCounter counter;
    SW_EXPECT_EQUAL( uint64{ 0 }, counter.get() );
    counter.add();
    counter.add( 4 );
    SW_EXPECT_EQUAL( uint64{ 5 }, counter.get() );
    counter.reset();
    SW_EXPECT_EQUAL( uint64{ 0 }, counter.get() );
}
