#include "pch.h"

#include "App/Frame/FrameTimeline.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;

// ------------------------------------------------------------------------------
// 1) FrameTimelineTest — 가변 델타 클램프 · 고정 스텝 상한 · 잔액 처리
//
// 이 정책이 무너지면 증상은 "무거운 장면에서 게임이 점점 더 느려진다" 다. 프레임이 길어질수록
// 고정 스텝을 더 부르고 그래서 더 길어지는 되먹임이라, 한 번 빠지면 스스로 빠져나오지 못한다.
// 그런데 그 되먹임은 **느린 프레임에서만** 나타나므로 실행해 봐서는 좀처럼 걸리지 않는다.
//
// 시간을 진짜로 잰다(주입할 시계가 없다). 그래서 단언은 **한 방향으로만** 건다 — "충분히 오래
// 잤으니 상한에서 잘렸다" 는 잠이 더 길어져도 참이다. 반대로 짧은 쪽을 봐야 할 때는 재우지 않고
// 곧바로 부른다(잠의 정밀도에 기대지 않는다).
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 최대 델타보다 확실히 긴 시간. 잠이 길어져도 결과는 같다(상한에서 잘리므로). */
    void sleepLongerThanAnyClamp()
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
    }
} // namespace

/**
 * @brief [FrameTimelineTest] 가변 델타는 설정한 최대값에서 잘리고, 고정 스텝 수는 그 몫이다
 */
SW_TEST_CASE( FrameTimelineTest, VariableDeltaIsClampedAndSplitIntoFixedSteps )
{
    FrameTimeline timeline;
    // 2의 거듭제곱 분수로 준다 — 몫이 부동소수 오차 없이 딱 떨어진다(0.25 / 0.0625 = 4).
    timeline.configure( 0.25f, 0.0625f, 8 );
    timeline.start();

    sleepLongerThanAnyClamp();
    const FrameTime frame = timeline.advance();

    SW_EXPECT_NEAR_EQUAL( 0.25f, frame._deltaTime, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.0625f, frame._fixedDeltaTime, 0.0001f );
    SW_EXPECT_EQUAL( 4u, frame._fixedStepCount );
}

/**
 * @brief [FrameTimelineTest] 스텝 수는 상한에서 잘리고, **넘친 잔액은 버린다**
 * @details 상한만 두고 잔액을 남기면 다음 프레임이 더 많은 스텝을 요구한다 — 상한이 있어도
 *          빚이 쌓여 영영 못 따라잡는다. 시뮬레이션이 실시간보다 느려지는 쪽을 택한 자리다.
 *          두 번째 호출은 **재우지 않는다** — 잔액을 남겼다면 잘 시간 없이도 스텝이 나온다.
 */
SW_TEST_CASE( FrameTimelineTest, StepCountIsCappedAndTheOverflowIsDropped )
{
    FrameTimeline timeline;
    // 0.25초가 쌓이면 4스텝이 필요하지만 상한은 2 — 0.125초가 남는다.
    timeline.configure( 0.25f, 0.0625f, 2 );
    timeline.start();

    sleepLongerThanAnyClamp();
    const FrameTime capped = timeline.advance();
    SW_EXPECT_EQUAL( 2u, capped._fixedStepCount );

    const FrameTime next = timeline.advance();
    SW_EXPECT_TRUE_MSG( next._fixedStepCount == 0u,
                        "버려야 할 잔액이 다음 프레임으로 넘어왔다 — 고정 스텝 스파이럴이다" );
}

/**
 * @brief [FrameTimelineTest] 0 이하 설정은 내장 기본값으로 되돌린다
 * @details 설정 파일 하나가 프레임 루프를 세우지 못하게 하는 자리다. 고정 델타가 0 이면
 *          스텝 수 계산이 0 으로 나누기가 된다.
 */
SW_TEST_CASE( FrameTimelineTest, NonPositiveConfigurationFallsBackToDefaults )
{
    FrameTimeline timeline;
    timeline.configure( -1.0f, 0.0f, 0 );
    timeline.start();

    sleepLongerThanAnyClamp();
    const FrameTime frame = timeline.advance();

    SW_EXPECT_NEAR_EQUAL( FrameTimeline::kDefaultMaxFrameDeltaTime, frame._deltaTime, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( FrameTimeline::kDefaultFixedDeltaTime, frame._fixedDeltaTime, 0.0001f );
    SW_EXPECT_TRUE_MSG( frame._fixedStepCount <= FrameTimeline::kDefaultMaxFixedStepPerFrame,
                        "기본 상한을 넘겨 스텝을 돌렸다" );
    // 기본값 둘의 몫은 6 이다 — 부동소수 나눗셈이라 5 로 떨어질 수 있어 아래를 열어 둔다.
    SW_EXPECT_TRUE( frame._fixedStepCount >= 5u );
}

/**
 * @brief [FrameTimelineTest] 만들어 두고 한참 뒤에 start 해도 첫 델타는 "방금" 부터다
 * @details `App` 은 이 객체를 멤버로 만들어 두고 창·RHI·모듈을 다 세운 뒤에야 `start()` 를
 *          부른다. 그 사이의 시간이 첫 델타에 섞이면 첫 프레임이 상한 가득한 고정 스텝을 돌아
 *          **시작하자마자 한 박자 건너뛴 것처럼** 보인다. `CpuTimer` 에 "첫 델타가 부팅 이후
 *          시간" 이라는 함정이 실제로 있었다 — 그 자리를 이 층에서도 막아 둔다.
 */
SW_TEST_CASE( FrameTimelineTest, StartRewindsTheTimerBeforeTheFirstFrame )
{
    FrameTimeline timeline;
    timeline.configure( 0.25f, 0.0625f, 8 );

    // 만든 시점과 루프 진입 사이에 시간이 흐른다.
    sleepLongerThanAnyClamp();
    timeline.start();

    // 루프에 들어가 한 프레임을 보낸다. 잰 값은 이 100ms 여야 한다 — 앞의 300ms 가 섞였다면
    // 최대 델타(0.25)에 붙고, 타이머가 아예 돌지 않았다면 0 이다. 양쪽을 다 본다.
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    const FrameTime firstFrame = timeline.advance();

    SW_EXPECT_TRUE_MSG( firstFrame._deltaTime > 0.05f, "start 뒤에 타이머가 돌지 않았다" );
    SW_EXPECT_TRUE_MSG( firstFrame._deltaTime < 0.22f, "start 이전의 시간이 첫 델타에 섞였다" );
}
