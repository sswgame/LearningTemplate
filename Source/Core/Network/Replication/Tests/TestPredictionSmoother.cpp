#include "pch.h"

#include "Core/Network/Replication/PredictionSmoother.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [PredictionSmootherTest] 보정 오프셋은 반감기마다 반으로 줄고, 끊는 거리를 넘는 보정은 녹이지 않고 바로 0 이다. 쓸 수 없는 설정은 거절한다
 */
SW_TEST_CASE( PredictionSmootherTest, CorrectionsFadeByHalfLifeAndLargeOnesSnap )
{
    PredictionSmoother smoother;
    SW_EXPECT_FALSE( smoother.initialize( PredictionSmootherSettings{ 0.0f, 2.0f } ) );
    SW_REQUIRE_TRUE( smoother.initialize( PredictionSmootherSettings{ 0.1f, 2.0f } ) );

    smoother.addCorrection( float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f } ); // 보정 전에 1 앞에 그렸다
    SW_EXPECT_NEAR( 1.0f, smoother.getOffset()._x, 0.0001f );
    smoother.update( 0.1f );
    SW_EXPECT_NEAR( 0.5f, smoother.getOffset()._x, 0.0001f );
    smoother.update( 0.05f );
    smoother.update( 0.05f ); // 나눠 흘려도 같은 시간이면 같은 만큼 준다
    SW_EXPECT_NEAR( 0.25f, smoother.getOffset()._x, 0.0001f );

    smoother.addCorrection( float3{ 5.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f } ); // 순간 이동 — 녹이지 않는다
    SW_EXPECT_NEAR( 0.0f, smoother.getOffset().getLength(), 0.0001f );
}
