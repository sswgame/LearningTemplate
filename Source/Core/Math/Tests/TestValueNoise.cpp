#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/ValueNoise.h"
#include "Core/Testing/TestFramework.h"

// ValueNoiseTest — 좌표 해시(`HashUtil::hashCoord` — 상태 없음, 좌표 · 씨앗에 민감, constexpr)와 값 노이즈(격자점 = 해시, 0..1, 연속, 옥타브 1 미만은 1). nogpu.

/**
 * @brief [ValueNoiseTest] 좌표 해시는 상태 없이 같은 입력에 같은 값이고, 좌표 · 씨앗 · 차원이 바뀌면 다른 값이며, 단위 실수는 [0, 1] 이다
 */
SW_TEST_CASE( ValueNoiseTest, CoordHashIsStatelessAndSensitive )
{
    SW_EXPECT_EQUAL( sw::HashUtil::hashCoord( 3, -7, 11u ), sw::HashUtil::hashCoord( 3, -7, 11u ) );
    SW_EXPECT_NOT_EQUAL( sw::HashUtil::hashCoord( 3, -7, 11u ), sw::HashUtil::hashCoord( -7, 3, 11u ) ); // 축을 맞바꾸면 다르다
    SW_EXPECT_NOT_EQUAL( sw::HashUtil::hashCoord( 3, -7, 11u ), sw::HashUtil::hashCoord( 3, -7, 12u ) );
    SW_EXPECT_NOT_EQUAL( sw::HashUtil::hashCoord( 3, 0, -7, 11u ), sw::HashUtil::hashCoord( 3, 1, -7, 11u ) );

    static_assert( sw::HashUtil::hashCoord( 1, 2, 3u ) == sw::HashUtil::hashCoord( 1, 2, 3u ), "the coordinate hash is constexpr" );
    SW_EXPECT_EQUAL( 0.0f, sw::HashUtil::toUnitFloat( 0xff000000u ) ); // 아래 24 비트만 본다
    SW_EXPECT_EQUAL( 1.0f, sw::HashUtil::toUnitFloat( 0x00ffffffu ) );
    for ( int32 x = -50; x < 50; ++x )
    {
        const float32 unit = sw::HashUtil::toUnitFloat( sw::HashUtil::hashCoord( x, x * 3, 7u ) );
        SW_REQUIRE_TRUE( 0.0f <= unit && unit <= 1.0f );
    }
}

/**
 * @brief [ValueNoiseTest] 값 노이즈: 정수 격자점에서는 격자 해시 그대로, 어디서나 0..1, 가까운 두 점은 가깝고, 옥타브 1 미만은 1 과 같다
 */
SW_TEST_CASE( ValueNoiseTest, ValueNoiseHitsLatticeAndStaysContinuous )
{
    constexpr uint32 kSeed = 42u;
    SW_EXPECT_NEAR( sw::ValueNoise::hashLattice( 4, -3, kSeed ), sw::ValueNoise::sampleValue( 4.0f, -3.0f, kSeed ), 1e-6 );
    SW_EXPECT_NEAR( sw::ValueNoise::hashLattice( -1, -1, kSeed ), sw::ValueNoise::sampleValue( -1.0f, -1.0f, kSeed ), 1e-6 ); // 음수 칸도 내림으로 찾는다

    float32 previous = sw::ValueNoise::sampleValue( 0.0f, 0.5f, kSeed );
    for ( int32 step = 1; step <= 400; ++step )
    {
        const float32 x     = static_cast<float32>( step ) * 0.01f;
        const float32 value = sw::ValueNoise::sampleValue( x, 0.5f, kSeed );
        SW_REQUIRE_TRUE( 0.0f <= value && value <= 1.0f );
        SW_REQUIRE_TRUE( sw::MathUtil::abs( value - previous ) < 0.05f ); // smoothstep 보간 — 0.01 걸음에 크게 뛰지 않는다
        previous = value;
    }

    SW_EXPECT_EQUAL( sw::ValueNoise::sampleValue( 1.3f, 2.7f, kSeed ), sw::ValueNoise::sampleFractal( 1.3f, 2.7f, kSeed, 1 ) );
    SW_EXPECT_EQUAL( sw::ValueNoise::sampleFractal( 1.3f, 2.7f, kSeed, 1 ), sw::ValueNoise::sampleFractal( 1.3f, 2.7f, kSeed, 0 ) );
    const float32 fractal = sw::ValueNoise::sampleFractal( 1.3f, 2.7f, kSeed, 5 );
    SW_EXPECT_TRUE( 0.0f <= fractal && fractal <= 1.0f );
    SW_EXPECT_NOT_EQUAL( sw::ValueNoise::sampleValue( 1.3f, 2.7f, kSeed ), sw::ValueNoise::sampleValue( 1.3f, 2.7f, kSeed + 1 ) );
}
