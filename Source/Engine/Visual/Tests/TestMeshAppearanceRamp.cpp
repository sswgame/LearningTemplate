#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Visual/Component/MeshComponent.h"

// MeshAppearanceRampTest — 메시 디더 등장 램프: 시간 0 인 걸음은 램프를 끝내지 않고, 오르는 램프는 1 에서 · 내리는 램프는 0 에서만 끝난다(nogpu).

/**
 * @brief [MeshAppearanceRampTest] 0 에서 오르기 시작한 램프는 시간 0 인 걸음에 끝나지 않고 1 에 닿을 때 끝난다 — 예전에는 0 에서 끝난 것으로 굳어 플레이어 몸이 그림자만 남았다
 */
SW_TEST_CASE( MeshAppearanceRampTest, RisingRampFromZeroSurvivesZeroDeltaAndEndsAtOne )
{
    float32 appearance = 0.0f;
    float32 rate       = 4.0f; // 0.25 초에 다 나타난다
    SW_EXPECT_TRUE( sw::MeshComponent::stepAppearanceRamp( appearance, rate, 0.0f ) );
    SW_EXPECT_EQUAL( 0.0f, appearance );
    SW_EXPECT_EQUAL( 4.0f, rate );
    SW_EXPECT_TRUE( sw::MeshComponent::stepAppearanceRamp( appearance, rate, 0.125f ) );
    SW_EXPECT_NEAR( 0.5f, appearance, 1e-6f );
    SW_EXPECT_FALSE( sw::MeshComponent::stepAppearanceRamp( appearance, rate, 0.5f ) );
    SW_EXPECT_EQUAL( 1.0f, appearance );
    SW_EXPECT_EQUAL( 0.0f, rate );
}

/**
 * @brief [MeshAppearanceRampTest] 1 에서 내리기 시작한 램프는 시간 0 인 걸음에 끝나지 않고 0 에 닿을 때 끝나며, 속도 0 은 할 일이 없다
 */
SW_TEST_CASE( MeshAppearanceRampTest, FallingRampEndsAtZeroAndIdleRampDoesNothing )
{
    float32 appearance = 1.0f;
    float32 rate       = -2.0f;
    SW_EXPECT_TRUE( sw::MeshComponent::stepAppearanceRamp( appearance, rate, 0.0f ) );
    SW_EXPECT_EQUAL( 1.0f, appearance );
    SW_EXPECT_FALSE( sw::MeshComponent::stepAppearanceRamp( appearance, rate, 1.0f ) );
    SW_EXPECT_EQUAL( 0.0f, appearance );
    SW_EXPECT_EQUAL( 0.0f, rate );

    float32 idleRate = 0.0f;
    SW_EXPECT_FALSE( sw::MeshComponent::stepAppearanceRamp( appearance, idleRate, 1.0f ) );
    SW_EXPECT_EQUAL( 0.0f, appearance );
}
