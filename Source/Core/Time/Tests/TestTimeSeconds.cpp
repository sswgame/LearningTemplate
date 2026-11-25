// TimeSecondsTest — 시계 꼬리표가 붙은 초 길이(GameSeconds · RealSeconds · StepSeconds): 서로 · float 와 암시적으로 바뀌지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/TimeSeconds.h"

#include <functional>
#include <type_traits>

using namespace sw;

// 결함 308250fe(입력 장치가 게임 시간을 받아 정지 동안 진동이 꺼지지 않았다) · a67dcef0(자막이 실제 시간으로 흘러 정지에도 넘어갔다): 다른 시계의 길이를 넣는 꼴이 컴파일되지 않는다.
static_assert( std::is_convertible_v<GameSeconds, RealSeconds> == false && std::is_convertible_v<RealSeconds, GameSeconds> == false, "game and real time must not mix" );
static_assert( std::is_convertible_v<StepSeconds, GameSeconds> == false && std::is_convertible_v<GameSeconds, StepSeconds> == false, "a fixed step is not a frame time" );
// 결함 2846c7ad(서버 틱 대신 프레임 시간 float 를 흘렸다): float 를 그대로 넣거나 꺼내는 꼴이 컴파일되지 않는다 — 시계를 정한 자리에서 명시적으로 만든다.
static_assert( std::is_convertible_v<float32, GameSeconds> == false && std::is_convertible_v<GameSeconds, float32> == false, "a float is not a time length of a known clock" );
static_assert( std::is_convertible_v<float32, RealSeconds> == false && std::is_convertible_v<RealSeconds, float32> == false, "a float is not a time length of a known clock" );
static_assert( std::is_invocable_v<std::plus<>, GameSeconds, GameSeconds> && std::is_invocable_v<std::plus<>, GameSeconds, RealSeconds> == false, "only the same clock adds" );

/** @brief [TimeSecondsTest] 같은 시계끼리 더하고 빼고 견주며, 배율을 곱하고, 비율은 수다 */
SW_TEST_CASE( TimeSecondsTest, SameClockArithmetic )
{
    constexpr GameSeconds frame{ 0.5f };
    GameSeconds           total = frame + frame;
    total -= GameSeconds{ 0.25f };
    SW_EXPECT_NEAR( 0.75f, total.count(), 1e-6f );
    SW_EXPECT_TRUE( frame < total );
    SW_EXPECT_NEAR( 1.5f, total / frame, 1e-6f );
    SW_EXPECT_NEAR( 0.25f, ( frame * 0.5f ).count(), 1e-6f );
    SW_EXPECT_EQUAL( 0.0f, RealSeconds{}.count() );
}
