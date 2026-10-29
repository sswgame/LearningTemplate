#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/View/RenderScaleAdjuster.h"

// 자동 렌더 배율 조정기 — 예산을 깨면 내리고, 하한에서 멈추고, 여유가 오래 가야 올리고, 틀린 설정은 거절한다. 렌더러 없는 순수 계산.

using namespace sw;

namespace
{
    RenderScaleAdjusterConfig makeConfig()
    {
        RenderScaleAdjusterConfig config;
        config._downFrames = 3;
        config._upFrames   = 5;
        return config;
    }

    void feed( RenderScaleAdjuster& adjuster, float32 frameMs, uint32 count )
    {
        for ( uint32 index = 0; index < count; ++index )
        {
            adjuster.update( frameMs );
        }
    }

    bool isNear( float32 a, float32 b )
    {
        return MathUtil::abs( a - b ) < 1.0e-5f;
    }
} // namespace

SW_TEST_CASE( RenderScaleAdjusterTest, StaysAtOneWithinBudget )
{
    RenderScaleAdjuster adjuster;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    feed( adjuster, 16.7f, 100 ); // 예산 그대로(수직동기)는 깬 것이 아니다
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, StepsDownAfterConsecutiveOverBudget )
{
    RenderScaleAdjuster adjuster;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    feed( adjuster, 25.0f, 2 );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
    adjuster.update( 10.0f ); // 연속이 끊기면 다시 센다
    feed( adjuster, 25.0f, 2 );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
    feed( adjuster, 25.0f, 1 );
    SW_EXPECT_TRUE( isNear( 0.9f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, StopsAtMinScale )
{
    RenderScaleAdjuster adjuster;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    feed( adjuster, 40.0f, 300 );
    SW_EXPECT_TRUE( isNear( 0.5f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, StepsUpSlowlyAndCapsAtOne )
{
    RenderScaleAdjuster adjuster;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    feed( adjuster, 40.0f, 6 ); // 0.8
    feed( adjuster, 8.0f, 4 );
    SW_EXPECT_TRUE( isNear( 0.8f, adjuster.getScale() ) );
    feed( adjuster, 8.0f, 1 );
    SW_EXPECT_TRUE( isNear( 0.85f, adjuster.getScale() ) );
    feed( adjuster, 8.0f, 200 );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, MiddleBandHoldsScale )
{
    RenderScaleAdjuster adjuster;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    feed( adjuster, 40.0f, 3 );   // 0.9
    feed( adjuster, 15.0f, 200 ); // 여유(예산의 0.8배 = 13.4)도 깸(17.5)도 아닌 구간
    SW_EXPECT_TRUE( isNear( 0.9f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, RejectsInvalidConfig )
{
    RenderScaleAdjuster       adjuster;
    RenderScaleAdjusterConfig config = makeConfig();
    config._stepUp                   = config._stepDown; // 진동한다
    SW_EXPECT_TRUE( RenderScaleAdjuster::validate( config ).empty() == false );
    SW_EXPECT_TRUE( adjuster.configure( config ) == false );
    config           = makeConfig();
    config._minScale = 0.0f;
    SW_EXPECT_TRUE( adjuster.configure( config ) == false );
    config             = makeConfig();
    config._downFrames = 0;
    SW_EXPECT_TRUE( adjuster.configure( config ) == false );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.update( 100.0f ) ) ); // 설정 전에는 건드리지 않는다
}
