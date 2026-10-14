// 공용 값 노이즈(MeshModifierNoise) — 같은 입력이면 같은 출력, 범위 [-1,1], 격자점 · 정수 시간에서 해시 값이 고정(옛 복사본과 비트 단위로 같은 식).
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierNoise.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( MeshModifierNoiseTest, LatticeHashIsPinned )
{
    // 기대값은 옛 모디파이어 복사본의 식을 32 비트 정수로 그대로 따라 계산한 값이다
    SW_EXPECT_EQUAL( 0x00000000u, MeshModifierNoise::hashLattice( 0, 0, 0, 0u ) );
    SW_EXPECT_EQUAL( 0x48799545u, MeshModifierNoise::hashLattice( 1, 2, 3, 7u ) );
    SW_EXPECT_EQUAL( 0xE84109D1u, MeshModifierNoise::hashLattice( -1, 5, -2, 42u ) );
}

SW_TEST_CASE( MeshModifierNoiseTest, ValueNoise3DAtLatticePointIsLatticeValue )
{
    // 정수 격자점에서는 보간 가중치가 0 이라 격자점 값 그대로다
    const float32 expected = static_cast<float32>( 0x48799545u & 0xFFFFu ) / 32767.5f - 1.0f;
    SW_EXPECT_EQUAL( expected, MeshModifierNoise::valueNoise3D( float3{ 1.0f, 2.0f, 3.0f }, 7u ) );
    SW_EXPECT_EQUAL( -1.0f, MeshModifierNoise::valueNoise3D( float3{ 0.0f, 0.0f, 0.0f }, 0u ) );
}

SW_TEST_CASE( MeshModifierNoiseTest, ValueNoise3DIsDeterministicAndInRange )
{
    for ( uint32 index = 0; index < 500; ++index )
    {
        const float32 step = static_cast<float32>( index );
        const float3  point{ step * 0.137f - 20.0f, step * 0.071f + 3.5f, step * -0.293f };
        const float32 value = MeshModifierNoise::valueNoise3D( point, index % 13u );
        SW_EXPECT_EQUAL( value, MeshModifierNoise::valueNoise3D( point, index % 13u ) );
        SW_EXPECT_TRUE( value >= -1.0f && value <= 1.0f );
    }
}

SW_TEST_CASE( MeshModifierNoiseTest, HashToUnitIsPinned )
{
    SW_EXPECT_EQUAL( 0.0f, MeshModifierNoise::hashToUnit( 0u ) );
    SW_EXPECT_EQUAL( static_cast<float32>( 0x8990C0u ) / 16777216.0f, MeshModifierNoise::hashToUnit( 1u ) );
    SW_EXPECT_EQUAL( static_cast<float32>( 0x2EFCF7u ) / 16777216.0f, MeshModifierNoise::hashToUnit( 12345u ) );
}

SW_TEST_CASE( MeshModifierNoiseTest, ValueNoise1DIsDeterministicAndInRange )
{
    // 정수 시간에서는 그 칸의 해시 값 그대로다(씨앗 0 · 시간 1 → hashToUnit( 1 ) · 2 − 1)
    SW_EXPECT_EQUAL( MeshModifierNoise::hashToUnit( 1u ) * 2.0f - 1.0f, MeshModifierNoise::valueNoise1D( 0u, 1.0f ) );
    for ( uint32 index = 0; index < 500; ++index )
    {
        const float32 time  = static_cast<float32>( index ) * 0.173f - 40.0f;
        const float32 value = MeshModifierNoise::valueNoise1D( index % 7u, time );
        SW_EXPECT_EQUAL( value, MeshModifierNoise::valueNoise1D( index % 7u, time ) );
        SW_EXPECT_TRUE( value >= -1.0f && value <= 1.0f );
    }
}
