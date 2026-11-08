#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Lighting/HairShading.h"

// 머리카락 셰이딩(hair_shading.hlsli 의 C++ 사본)의 식을 CPU 에서 본다 — 갈래 넓이 · 구 적분(에너지) · 큐티클 이동 · 탄젠트 이동 · 흡수 색.

using namespace sw;

namespace
{
    /** @brief 가닥 +Y 에 대해 빛 방향을 구 위 격자(z 고르게 · 방위 고르게)로 돌며 갈래 합의 구 적분(입체각)을 구합니다. */
    float3 integrateOverSphere( const HairSurface& surface, const float3& toView )
    {
        constexpr uint32 kHeightSteps  = 240;
        constexpr uint32 kAzimuthSteps = 480;
        float3           sum{};
        for ( uint32 heightIndex = 0; heightIndex < kHeightSteps; ++heightIndex )
        {
            const float32 height = -1.0f + ( static_cast<float32>( heightIndex ) + 0.5f ) * 2.0f / static_cast<float32>( kHeightSteps );
            const float32 radius = MathUtil::sqrt( MathUtil::max( 1.0f - height * height, 0.0f ) );
            for ( uint32 azimuthIndex = 0; azimuthIndex < kAzimuthSteps; ++azimuthIndex )
            {
                const float32 azimuth = ( static_cast<float32>( azimuthIndex ) + 0.5f ) * MathUtil::kTwoPi / static_cast<float32>( kAzimuthSteps );
                const float3  toLight{ radius * MathUtil::cos( azimuth ), height, radius * MathUtil::sin( azimuth ) };
                sum += surface.evaluateLobes( toView, toLight );
            }
        }
        return sum * ( 4.0f * MathUtil::kPi / static_cast<float32>( kHeightSteps * kAzimuthSteps ) );
    }
} // namespace

// 세로 갈래는 넓이 1 인 가우스다 — 폭이 좁든 넓든 적분이 1 이다.
SW_TEST_CASE( HairShadingTest, LongitudinalLobeHasUnitArea )
{
    for ( const float32 width : { 0.02f, 0.09f, 0.3f } )
    {
        float32          area      = 0.0f;
        constexpr uint32 kSteps    = 4000;
        constexpr float  kHalfSpan = 2.0f;
        for ( uint32 index = 0; index < kSteps; ++index )
        {
            const float32 offset = -kHalfSpan + ( static_cast<float32>( index ) + 0.5f ) * 2.0f * kHalfSpan / static_cast<float32>( kSteps );
            area += HairSurface::computeLongitudinal( width, offset ) * 2.0f * kHalfSpan / static_cast<float32>( kSteps );
        }
        SW_EXPECT_NEAR( area, 1.0f, 1.0e-3f );
    }
}

// 흰 머리카락의 세 갈래 구 적분은 1 근처다(언리얼 근사의 에너지 — 빛 세기를 머티리얼마다 맞출 일이 없다). 시선을 바꿔도 같다.
SW_TEST_CASE( HairShadingTest, WhiteHairLobesIntegrateNearOne )
{
    HairSurface surface{};
    surface._baseColor     = float3{ 1.0f, 1.0f, 1.0f };
    surface._roughness     = 0.3f;
    const float3 arrView[] = {
        float3{0.0f,  0.0f, 1.0f},
        float3{0.0f,  0.5f, 1.0f}
            .normalize(),
        float3{0.3f, -0.4f, 1.0f}
            .normalize()
    };
    for ( const float3& toView : arrView )
    {
        const float3 energy = integrateOverSphere( surface, toView );
        SW_EXPECT_TRUE( energy._x > 0.85f && energy._x < 1.15f );
        SW_EXPECT_NEAR( energy._x, energy._z, 1.0e-4f ); // 흰색이면 채널이 같다
    }
}

// 검은 머리카락은 투과 갈래(TT · TRT)를 흡수해 R 만 남는다 — 구 적분이 R 의 몫(0.1 안쪽)으로 떨어진다.
SW_TEST_CASE( HairShadingTest, BlackHairKeepsOnlyTheSurfaceReflection )
{
    HairSurface surface{};
    surface._baseColor  = float3{ 0.0f, 0.0f, 0.0f };
    const float3 energy = integrateOverSphere( surface, float3{ 0.0f, 0.0f, 1.0f } );
    SW_EXPECT_TRUE( energy._x > 0.02f && energy._x < 0.1f );
}

// 큐티클 이동: R 하이라이트는 sin θi + sin θr = −2α 에서 가장 밝다(뿌리 쪽으로 옮겨 간다). 이동이 0 이면 정반사(합 0)가 가장 밝다.
SW_TEST_CASE( HairShadingTest, CuticleShiftMovesTheReflectionPeak )
{
    HairSurface surface{};
    surface._roughness = 0.25f;
    surface._shift     = 0.1f;
    const float3 toView{ 0.0f, 0.0f, 1.0f };
    float32      bestSum   = 0.0f;
    float32      bestValue = -1.0f;
    for ( int32 step = -60; step <= 60; ++step )
    {
        const float32 sinLight = static_cast<float32>( step ) / 100.0f;
        const float3  toLight{ 0.0f, sinLight, MathUtil::sqrt( 1.0f - sinLight * sinLight ) };
        const float32 value = surface.evaluateReflection( toView, toLight );
        if ( value > bestValue )
        {
            bestValue = value;
            bestSum   = sinLight;
        }
    }
    SW_EXPECT_NEAR( bestSum, -0.2f, 0.021f );
}

// 역광: 빛이 가닥 뒤(눈 반대쪽)에 있으면 투과(TT)가 앞에서 비출 때보다 훨씬 밝다.
SW_TEST_CASE( HairShadingTest, BacklightTransmitsMoreThanFrontLight )
{
    HairSurface surface{};
    surface._baseColor = float3{ 0.8f, 0.6f, 0.4f };
    const float3 toView{ 0.0f, 0.0f, 1.0f };
    const float3 behind = surface.evaluateLobes( toView, float3{ 0.0f, 0.0f, -1.0f } );
    const float3 front  = surface.evaluateLobes( toView, float3{ 0.05f, 0.0f, 1.0f }.normalize() );
    SW_EXPECT_TRUE( behind._x > front._x * 2.0f );
    SW_EXPECT_TRUE( behind._x > behind._z ); // 투과 빛은 흡수 색으로 물든다
}

// 탄젠트 이동: 노멀과 수직인 탄젠트를 shift 만큼 기울이면 단위 벡터이고 기운 각이 atan(shift) 다. 0 이면 그대로다.
SW_TEST_CASE( HairShadingTest, ShiftTangentTiltsTowardTheNormal )
{
    const float3 tangent{ 0.0f, 1.0f, 0.0f };
    const float3 normal{ 0.0f, 0.0f, 1.0f };
    const float3 same = HairSurface::shiftTangent( tangent, normal, 0.0f );
    SW_EXPECT_NEAR( ( same - tangent ).getLength(), 0.0f, 1.0e-6f );
    const float3 tilted = HairSurface::shiftTangent( tangent, normal, 0.2f );
    SW_EXPECT_NEAR( tilted.getLength(), 1.0f, 1.0e-5f );
    SW_EXPECT_NEAR( MathUtil::acos( tilted.dot( tangent ) ), MathUtil::atan2( 0.2f, 1.0f ), 1.0e-4f );
    SW_EXPECT_TRUE( tilted._z > 0.0f );
}
