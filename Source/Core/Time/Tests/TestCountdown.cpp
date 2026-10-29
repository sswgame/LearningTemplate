// Countdown · RateAccumulator — 끝난 걸음에 한 번 알리고 늦음을 한 간격까지 잇는지, 반복 빈도가 프레임률에 매이지 않는지, 비율의 분수 몫을 넘기는지.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Countdown.h"

using namespace sw;

namespace
{
    /** @brief 고정 dt 로 @p seconds 동안 끝날 때마다 다시 걸어 낸 횟수입니다(게임 루프 순서 — 흘리고, 끝났으면 내고 다시 건다). */
    int32 countRepeatsFor( float32 interval, float32 framesPerSecond, float32 seconds )
    {
        Countdown     cooldown;
        const float32 deltaTime  = 1.0f / framesPerSecond;
        const int32   frameCount = static_cast<int32>( seconds * framesPerSecond + 0.5f );
        int32         count      = 0;
        for ( int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            cooldown.update( deltaTime );
            if ( cooldown.isActive() )
                continue;
            ++count;
            cooldown.restart( interval );
        }
        return count;
    }
} // namespace

/**
 * @brief [CountdownTest] 남은 시간은 끝난 걸음에 한 번 알리고, 그 걸음에 다시 걸면 지나친 몫(늦음)을 한 간격까지 다음 간격에서 뺀다
 * @details 쉬다가 다시 걸면 늦음을 잇지 않는다(다음 `tick` 이 지운다). 0 이하 걸음은 아무것도 하지 않는다. `extendTo` 는 더 길 때만 늘린다.
 */
SW_TEST_CASE( CountdownTest, CountdownEndsOnceAndRestartCarriesOneIntervalOfLateness )
{
    Countdown countdown;
    SW_EXPECT_FALSE( countdown.isActive() );
    SW_EXPECT_FALSE( countdown.update( 0.1f ) ); // 꺼진 채 흘려도 끝나지 않는다
    countdown.start( 1.0f );
    SW_EXPECT_FALSE( countdown.update( 0.6f ) );
    SW_EXPECT_TRUE( countdown.isActive() );
    SW_EXPECT_NEAR( 0.4f, countdown.getRemaining(), 1.0e-5f );
    SW_EXPECT_FALSE( countdown.update( 0.0f ) ); // 0 걸음은 그대로
    SW_EXPECT_TRUE( countdown.update( 0.6f ) );  // 이번 걸음에 끝났다 — 0.2 지나쳤다
    SW_EXPECT_FALSE( countdown.isActive() );
    SW_EXPECT_NEAR( 0.0f, countdown.getRemaining(), 1.0e-6f );
    countdown.restart( 1.0f );
    SW_EXPECT_NEAR( 0.8f, countdown.getRemaining(), 1.0e-5f ); // 늦은 0.2 를 뺐다
    SW_EXPECT_FALSE( countdown.update( 0.7f ) );
    SW_EXPECT_TRUE( countdown.update( 0.7f ) );
    SW_EXPECT_FALSE( countdown.update( 0.1f ) ); // 다시 걸지 않았다 — 늦음이 지워지고 다시 알리지 않는다
    countdown.restart( 1.0f );
    SW_EXPECT_NEAR( 1.0f, countdown.getRemaining(), 1.0e-6f ); // 쉰 시간은 잇지 않는다

    // 걸음이 간격보다 길면 잇는 몫은 한 간격까지 — 결과가 0 이어도 걸음마다 한 번이지 몰아 내지 않는다.
    countdown.start( 0.1f );
    SW_EXPECT_TRUE( countdown.update( 0.5f ) );
    countdown.restart( 0.25f );
    SW_EXPECT_FALSE( countdown.isActive() );
    SW_EXPECT_FALSE( countdown.update( 0.5f ) );
    SW_EXPECT_FALSE( countdown.isActive() );

    // 아직 남아 있을 때 다시 걸면 새로 거는 것과 같다.
    countdown.start( 0.5f );
    countdown.restart( 2.0f );
    SW_EXPECT_NEAR( 2.0f, countdown.getRemaining(), 1.0e-6f );
    countdown.extendTo( 1.0f );
    SW_EXPECT_NEAR( 2.0f, countdown.getRemaining(), 1.0e-6f );
    countdown.extendTo( 3.0f );
    SW_EXPECT_NEAR( 3.0f, countdown.getRemaining(), 1.0e-6f );
    countdown.clear();
    SW_EXPECT_FALSE( countdown.isActive() );
}

/**
 * @brief [CountdownTest] 끝난 걸음에 `restart` 로 다시 거는 반복은 프레임률과 상관없이 설계 빈도(시간 / 간격)를 낸다
 * @details 0.095 초 간격을 30 · 60 · 144 fps 로 10 초 — 설계 105.26 회 ±1. 늦음을 버리면(`start`) 간격이 `ceil( 간격 / dt ) × dt` 로 늘어
 *          30 · 60 fps 에서 100 회가 된다. 간격보다 긴 프레임(5 fps)은 프레임마다 한 번 — 50 회이고 몰아 내지 않는다.
 */
SW_TEST_CASE( CountdownTest, CountdownRepeatRateDoesNotDependOnFrameRate )
{
    const float32 interval = 0.095f;
    const float32 seconds  = 10.0f;
    for ( const float32 framesPerSecond : { 30.0f, 60.0f, 144.0f } )
    {
        SW_EXPECT_NEAR( seconds / interval, static_cast<float32>( countRepeatsFor( interval, framesPerSecond, seconds ) ), 1.0f );
    }
    SW_EXPECT_EQUAL( 50, countRepeatsFor( interval, 5.0f, seconds ) );
}

/**
 * @brief [CountdownTest] `tickRepeat` — 누르고 있는 동안 간격마다 한 번, 지나친 몫을 이어 빈도가 걸음 크기에 매이지 않고, 쉬면 늦음을 버린다
 * @details 테스트 게임 둘이 이 줄을 쓴다 — VoxelCraft 블록 놓기(0.25 초 간격, 걸음 0.1 초 상한)와 Shooter3D 적 휘두르기(1.5 초). 손으로 세던 판
 *          (`x -= dt; if ( want && x <= 0 ) x = interval;`)은 0.1 초 걸음에서 놓기 간격을 0.3 초로 늘렸다 — 10 초 누르면 41 이 아니라 34 번.
 */
SW_TEST_CASE( CountdownTest, CountdownTickRepeatKeepsTheHeldRate )
{
    BLOCK( "VoxelCraft 블록 놓기 — 0.25 초 간격을 0.1 초 걸음 · 60 fps 로 10 초 누르고 있으면 첫 누름 + 40 번" )
    {
        for ( const float32 deltaTime : { 0.1f, 1.0f / 60.0f } )
        {
            Countdown   placeCooldown;
            const int32 stepCount  = static_cast<int32>( 10.0f / deltaTime + 0.5f );
            int32       placeCount = 0;
            for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            {
                placeCount += placeCooldown.updateRepeat( deltaTime, 0.25f, true ) ? 1 : 0;
            }
            SW_EXPECT_NEAR( 41.0f, static_cast<float32>( placeCount ), 1.0f );
        }
    }
    BLOCK( "Shooter3D 적 휘두르기 — 사정거리 밖에서 쉰 시간은 잇지 않는다(들어오면 한 번, 몰아 휘두르지 않는다)" )
    {
        Countdown swingCooldown;
        swingCooldown.start( 0.75f );
        int32 swingCount = 0;
        for ( int32 stepIndex = 0; stepIndex < 50; ++stepIndex ) // 5 초 사정거리 밖
        {
            swingCount += swingCooldown.updateRepeat( 0.1f, 1.5f, false ) ? 1 : 0;
        }
        SW_EXPECT_EQUAL( 0, swingCount );
        SW_EXPECT_TRUE( swingCooldown.updateRepeat( 0.1f, 1.5f, true ) ); // 들어온 걸음에 한 번
        SW_EXPECT_FALSE( swingCooldown.updateRepeat( 0.1f, 1.5f, true ) );
        SW_EXPECT_NEAR( 1.4f, swingCooldown.getRemaining(), 1.0e-4f );
    }
}

/**
 * @brief [CountdownTest] 비율 누적기는 분수 몫을 다음 걸음으로 넘겨 긴 시간의 합이 비율 × 시간이 된다 — 하나씩 · 정수 부분 통째로
 */
SW_TEST_CASE( CountdownTest, RateAccumulatorCarriesTheFractionAcrossSteps )
{
    RateAccumulator arrival;
    int32           arrivalCount = 0;
    for ( int32 stepIndex = 0; stepIndex < 100; ++stepIndex )
    {
        arrival.add( 0.25f ); // 2 진수로 정확한 몫 — 합이 정확히 25
        while ( arrival.takeOne() )
        {
            ++arrivalCount;
        }
    }
    SW_EXPECT_EQUAL( 25, arrivalCount );
    SW_EXPECT_TRUE( 0.0f <= arrival.getFraction() && arrival.getFraction() < 1.0f );

    RateAccumulator cost;
    int32           costTotal = 0;
    for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
    {
        cost.add( 2.5f / 60.0f * 7.0f ); // 초당 2.5 를 7 초짜리 걸음 60 번
        costTotal += cost.takeWhole();
    }
    SW_EXPECT_TRUE( MathUtil::abs( costTotal - 17 ) <= 1 );
    cost.reset();
    SW_EXPECT_NEAR( 0.0f, cost.getFraction(), 1.0e-6f );
}
