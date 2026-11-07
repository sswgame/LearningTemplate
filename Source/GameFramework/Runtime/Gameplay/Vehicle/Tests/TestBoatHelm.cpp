#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/BoatHelmComponent.h"

// BoatHelmTest — 의도(가고 싶은 월드 방향)를 배의 추진 · 키로 바꾸는 규칙. 물리 씬 없이 돈다. 실제 항해는 BoatTest(엔진)와 시나리오 game.empty.boat 가 본다.

using namespace sw;

/** @brief [BoatHelmTest] 선수 쪽 의도는 추진, 옆 의도는 키다. 뒤쪽 의도는 뒤로 젓기(음수 추진)이고, 선수가 +X 를 보면 +Z 의도는 왼쪽 키다 */
SW_TEST_CASE( BoatHelmTest, IntentBecomesThrottleAndRudderAlongTheBow )
{
    ControlIntent intent;
    intent._move        = float2{ 0.6f, 0.8f };
    BoatHelmInput input = BoatHelmComponent::toHelmInput( intent, float3{ 0.0f, 0.0f, 1.0f } );
    SW_EXPECT_NEAR( 0.8f, input._throttle, 1.0e-4f );
    SW_EXPECT_NEAR( 0.6f, input._rudder, 1.0e-4f );

    intent._move = float2{ 0.0f, -1.0f };
    input        = BoatHelmComponent::toHelmInput( intent, float3{ 0.0f, 0.0f, 1.0f } );
    SW_EXPECT_NEAR( -1.0f, input._throttle, 1.0e-4f );

    intent._move = float2{ 0.0f, 1.0f };
    input        = BoatHelmComponent::toHelmInput( intent, float3{ 1.0f, 0.3f, 0.0f } ); // 기운 선체 — 수평 성분만 본다
    SW_EXPECT_NEAR( 0.0f, input._throttle, 1.0e-4f );
    SW_EXPECT_NEAR( -1.0f, input._rudder, 1.0e-4f );
}
