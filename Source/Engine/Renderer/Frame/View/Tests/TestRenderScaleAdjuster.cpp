#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Frame/View/RenderScaleAdjuster.h"

// 자동 렌더 배율 조정기 — GPU 프레임 표본으로 예산을 깨면 내리고, 하한에서 멈추고, 여유가 오래 가야 올리고, 같은 순번은 한 번만 세고,
// 단계 뒤 표본을 버리고, 타임스탬프 없는 시계면 끄고, 틀린 설정은 거절한다. 렌더러 없는 순수 계산.

using namespace sw;

namespace
{
    /** @brief `Config/Engine/RenderScale.json` 과 같은 모양의 설정입니다(연속 수만 짧게). 버리는 표본은 0 — 따로 보는 테스트가 켠다. */
    RenderScaleAdjusterConfig makeConfig()
    {
        RenderScaleAdjusterConfig config;
        config._budgetMs       = 16.7f;
        config._minScale       = 0.5f;
        config._stepDown       = 0.1f;
        config._stepUp         = 0.05f;
        config._overRatio      = 1.05f;
        config._headroomRatio  = 0.8f;
        config._downFrames     = 3;
        config._upFrames       = 5;
        config._discardSamples = 0;
        return config;
    }

    /** @brief 순번이 하나씩 오르는 표본을 @p count 개 넣습니다(렌더 스레드가 프레임마다 하나씩 읽은 것과 같다). */
    class SampleSequence
    {
    public:
        void feed( RenderScaleAdjuster& adjuster, float32 gpuMs, uint32 count )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                ++_serial;
                adjuster.update( GPUFrameSample{ _serial, gpuMs } );
            }
        }

    private:
        uint32 _serial{ 0 };
    };

    bool isNear( float32 a, float32 b )
    {
        return MathUtil::abs( a - b ) < 1.0e-5f;
    }
} // namespace

SW_TEST_CASE( RenderScaleAdjusterTest, StaysAtOneWithinBudget )
{
    RenderScaleAdjuster adjuster;
    SampleSequence      samples;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    samples.feed( adjuster, 16.7f, 100 ); // 예산 그대로는 깬 것이 아니다
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, StepsDownAfterConsecutiveOverBudget )
{
    RenderScaleAdjuster adjuster;
    SampleSequence      samples;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    samples.feed( adjuster, 25.0f, 2 );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
    samples.feed( adjuster, 10.0f, 1 ); // 연속이 끊기면 다시 센다
    samples.feed( adjuster, 25.0f, 2 );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
    samples.feed( adjuster, 25.0f, 1 );
    SW_EXPECT_TRUE( isNear( 0.9f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, StopsAtMinScale )
{
    RenderScaleAdjuster adjuster;
    SampleSequence      samples;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    samples.feed( adjuster, 40.0f, 300 );
    SW_EXPECT_TRUE( isNear( 0.5f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, StepsUpSlowlyAndCapsAtOne )
{
    RenderScaleAdjuster adjuster;
    SampleSequence      samples;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    samples.feed( adjuster, 40.0f, 6 ); // 0.8
    samples.feed( adjuster, 8.0f, 4 );
    SW_EXPECT_TRUE( isNear( 0.8f, adjuster.getScale() ) );
    samples.feed( adjuster, 8.0f, 1 );
    SW_EXPECT_TRUE( isNear( 0.85f, adjuster.getScale() ) );
    samples.feed( adjuster, 8.0f, 200 );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, MiddleBandHoldsScale )
{
    RenderScaleAdjuster adjuster;
    SampleSequence      samples;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    samples.feed( adjuster, 40.0f, 3 );   // 0.9
    samples.feed( adjuster, 15.0f, 200 ); // 여유(예산의 0.8배 = 13.4)도 깸(17.5)도 아닌 구간
    SW_EXPECT_TRUE( isNear( 0.9f, adjuster.getScale() ) );
}

/**
 * @brief [RenderScaleAdjusterTest] 감사 RN-8 "16.67 ms 고정에서 배율 복귀": vsync 60 Hz 면 벽시계는 늘 16.67 ms 라 여유가 서지 않았다.
 *        GPU 시간은 부하가 걷히면 예산 아래로 내려가므로 `_upFrames` × 단계 수 표본 안에 1.0 으로 돌아온다.
 */
SW_TEST_CASE( RenderScaleAdjusterTest, ScaleReturnsWhenGPUHasHeadroomAtFixedVsyncInterval )
{
    RenderScaleAdjuster       adjuster;
    SampleSequence            samples;
    RenderScaleAdjusterConfig config = makeConfig();
    SW_EXPECT_TRUE( adjuster.configure( config ) );
    samples.feed( adjuster, 30.0f, config._downFrames * 5u ); // 무거운 장면 — 0.5 까지
    SW_EXPECT_TRUE( isNear( 0.5f, adjuster.getScale() ) );
    // 부하가 걷혔다. 화면은 여전히 16.67 ms 마다 바뀌지만(vsync) GPU 는 6 ms 만 쓴다.
    const uint32 stepCount = 10; // (1 - 0.5) / 0.05
    samples.feed( adjuster, 6.0f, config._upFrames * stepCount );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
}

/** @brief [RenderScaleAdjusterTest] 같은 순번의 표본(시계가 아직 새 프레임을 읽지 못했다)은 한 번만 센다. 순번 0(아직 없음)은 세지 않는다. */
SW_TEST_CASE( RenderScaleAdjusterTest, SameSerialCountsOnce )
{
    RenderScaleAdjuster adjuster;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    for ( uint32 index = 0; index < 10; ++index )
    {
        adjuster.update( GPUFrameSample{ 0, 40.0f } );
        adjuster.update( GPUFrameSample{ 7, 40.0f } );
    }
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) ); // 깬 표본은 하나뿐이다(내림은 셋)
    adjuster.update( GPUFrameSample{ 8, 40.0f } );
    adjuster.update( GPUFrameSample{ 9, 40.0f } );
    SW_EXPECT_TRUE( isNear( 0.9f, adjuster.getScale() ) );
}

/** @brief [RenderScaleAdjusterTest] 단계를 바꾼 뒤 `_discardSamples` 개의 표본은 옛 배율로 그린 프레임이라 버린다 — 연속으로 두 단계 내려가지 않는다. */
SW_TEST_CASE( RenderScaleAdjusterTest, SamplesAfterAStepAreDiscarded )
{
    RenderScaleAdjuster       adjuster;
    SampleSequence            samples;
    RenderScaleAdjusterConfig config = makeConfig();
    config._discardSamples           = 4;
    SW_EXPECT_TRUE( adjuster.configure( config ) );
    samples.feed( adjuster, 40.0f, 3 );
    SW_EXPECT_TRUE( isNear( 0.9f, adjuster.getScale() ) );
    samples.feed( adjuster, 40.0f, 4 + 2 ); // 버림 넷 + 둘 — 아직 셋이 아니다
    SW_EXPECT_TRUE( isNear( 0.9f, adjuster.getScale() ) );
    samples.feed( adjuster, 40.0f, 1 );
    SW_EXPECT_TRUE( isNear( 0.8f, adjuster.getScale() ) );
}

/** @brief [RenderScaleAdjusterTest] 타임스탬프가 없는 시계(Unsupported)면 배율을 1 로 되돌리고 꺼야 한다고 알린다. 데우는 중 · 꺼짐 · 준비는 받는다. */
SW_TEST_CASE( RenderScaleAdjusterTest, UnsupportedClockTurnsAutoScaleOff )
{
    RenderScaleAdjuster adjuster;
    SampleSequence      samples;
    SW_EXPECT_TRUE( adjuster.configure( makeConfig() ) );
    SW_EXPECT_TRUE( adjuster.acceptClockState( GPUFrameClockState::Off ) ); // 원한다는 표시가 렌더 스레드에 닿기 전
    SW_EXPECT_TRUE( adjuster.acceptClockState( GPUFrameClockState::WarmingUp ) );
    SW_EXPECT_TRUE( adjuster.acceptClockState( GPUFrameClockState::Ready ) );
    samples.feed( adjuster, 40.0f, 3 );
    SW_EXPECT_TRUE( isNear( 0.9f, adjuster.getScale() ) );

    GPUFrameClock clock;
    clock.setWanted( true );
    std::ignore = clock.readBack( nullptr, false ); // 타임스탬프 능력이 없는 디바이스
    SW_EXPECT_FALSE( adjuster.acceptClockState( clock.getState() ) );
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.getScale() ) );
}

SW_TEST_CASE( RenderScaleAdjusterTest, RejectsInvalidConfig )
{
    RenderScaleAdjuster       adjuster;
    RenderScaleAdjusterConfig config = makeConfig();
    config._stepUp                   = config._stepDown; // 진동한다
    SW_EXPECT_TRUE( config.validate().has_value() == false );
    SW_EXPECT_TRUE( adjuster.configure( config ) == false );
    config           = makeConfig();
    config._minScale = 0.0f;
    SW_EXPECT_TRUE( adjuster.configure( config ) == false );
    config             = makeConfig();
    config._downFrames = 0;
    SW_EXPECT_TRUE( adjuster.configure( config ) == false );
    SW_EXPECT_TRUE( RenderScaleAdjusterConfig{}.validate().has_value() == false );    // 파일이 빈 칸을 남기면 거절한다(기본값이 없다)
    SW_EXPECT_TRUE( isNear( 1.0f, adjuster.update( GPUFrameSample{ 1, 100.0f } ) ) ); // 설정 전에는 건드리지 않는다
}
