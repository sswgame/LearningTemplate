#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Movement/MoveAnimationComponent.h"

// MoveAnimationTest — 움직인 거리에서 고르게 한 속도, 요를 짧은 쪽으로 최대 걸음만큼 돌리기(nogpu).

/**
 * @brief [MoveAnimationTest] 고르기 0 은 그 걸음의 속도 그대로, 고르기가 있으면 목표 쪽으로 일부만 가고 멈춘 프레임(시간 0)은 이전 값 그대로다
 */
SW_TEST_CASE( MoveAnimationTest, SmoothSpeedFollowsSteps )
{
    SW_EXPECT_NEAR( 2.0f, sw::MoveAnimationComponent::smoothSpeed( 0.0f, 0.2f, 0.1f, 0.0f ), 1e-5f );
    const float32 smoothed = sw::MoveAnimationComponent::smoothSpeed( 0.0f, 0.2f, 0.1f, 0.15f );
    SW_EXPECT_TRUE( smoothed > 0.0f && smoothed < 2.0f );
    SW_EXPECT_NEAR( 1.5f, sw::MoveAnimationComponent::smoothSpeed( 1.5f, 0.0f, 0.0f, 0.15f ), 1e-6f );
}

/**
 * @brief [MoveAnimationTest] 요는 짧은 쪽으로 돌고(π 를 넘어 감싸지 않는다) 한 번에 최대 걸음까지만 돈다
 */
SW_TEST_CASE( MoveAnimationTest, TurnTowardsTakesShortWayAndClamps )
{
    const float32 pi = sw::MathUtil::kPi;
    SW_EXPECT_NEAR( 0.5f, sw::MoveAnimationComponent::turnTowards( 0.0f, 2.0f, 0.5f ), 1e-5f );
    // 0.9π 에서 -0.9π 로: 짧은 쪽은 +0.2π(감싸기)다 — 반대로 1.8π 를 돌지 않는다
    const float32 turned = sw::MoveAnimationComponent::turnTowards( 0.9f * pi, -0.9f * pi, 10.0f );
    SW_EXPECT_NEAR( 1.1f * pi, turned, 1e-4f );
    SW_EXPECT_NEAR( 1.0f, sw::MoveAnimationComponent::turnTowards( 1.0f, 1.0f, 0.5f ), 1e-6f );
}
