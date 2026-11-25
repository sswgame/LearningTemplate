// MaterialProjection — 3 축 투영 가중치와 타일 변형 해시. 셰이더(material_functions.hlsli)는 여기서 돌지 않으므로 같은 식의 CPU 값을 박는다.
// 기대값은 같은 식을 파이썬으로 따로 계산한 값이다 — 셰이더를 바꾸면 이 값도 함께 바뀌어야 한다.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/Material/MaterialProjection.h"

using namespace sw;

/**
 * @brief [MaterialProjectionTest] 3 축 가중치는 |n|^날카로움을 합으로 나눈다 - 축 하나를 보는 노멀은 그 축이 1, 비스듬한 노멀은 날카로울수록 큰 축으로 쏠린다
 * @details 변이: 절댓값을 빼면 아래를 보는 노멀의 y 가중치가 음수다. 지수를 빼면(날카로움 1 과 같다) 4 의 기대값이 진다.
 */
SW_TEST_CASE( MaterialProjectionTest, TriplanarWeightsFollowNormalPower )
{
    const float3 up = MaterialProjection::computeTriplanarWeights( float3{ 0.0f, 1.0f, 0.0f }, 4.0f );
    SW_EXPECT_NEAR( 1.0f, up._y, 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, up._x + up._z, 1.0e-5f );

    const float3 down = MaterialProjection::computeTriplanarWeights( float3{ 0.0f, -1.0f, 0.0f }, 4.0f );
    SW_EXPECT_NEAR( 1.0f, down._y, 1.0e-5f );

    const float3 sharp = MaterialProjection::computeTriplanarWeights( float3{ 0.6f, 0.8f, 0.0f }, 4.0f );
    SW_EXPECT_NEAR( 0.240356f, sharp._x, 1.0e-5f );
    SW_EXPECT_NEAR( 0.759644f, sharp._y, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, sharp._x + sharp._y + sharp._z, 1.0e-5f );

    const float3 soft = MaterialProjection::computeTriplanarWeights( float3{ 0.6f, 0.8f, 0.0f }, 1.0f );
    SW_EXPECT_NEAR( 0.428568f, soft._x, 1.0e-5f );
    SW_EXPECT_TRUE( sharp._y > soft._y ); // 날카로울수록 큰 축으로 쏠린다

    const float3 clamped = MaterialProjection::computeTriplanarWeights( float3{ 0.6f, 0.8f, 0.0f }, 0.0f );
    SW_EXPECT_NEAR( soft._x, clamped._x, 1.0e-6f ); // 1 보다 작은 날카로움은 1 이다
}

/**
 * @brief [MaterialProjectionTest] 타일 변형은 타일 · 씨앗만으로 정해진다 - 박힌 두 타일의 값, 범위, 이웃 타일이 다른 값, 넓은 격자에서 회전 넷이 모두 나온다
 * @details 변이: 소금을 빼면 타일 (0, 0) · 씨앗 0 이 0 해시라 첫 기대값이 진다. 축 곱수를 같게 두면 (x, y) 와 (y, x) 가 같아져 대칭 검사가 진다.
 */
SW_TEST_CASE( MaterialProjectionTest, TileVariationIsDeterministicAndSpread )
{
    const MaterialProjection::TileVariation origin = MaterialProjection::computeTileVariation( 0, 0, 0u );
    SW_EXPECT_NEAR( 0.735672f, origin._offset._x, 1.0e-5f );
    SW_EXPECT_NEAR( 0.040857f, origin._offset._y, 1.0e-5f );
    SW_EXPECT_EQUAL( 0u, origin._rotation );
    SW_EXPECT_NEAR( 0.274491f, origin._brightness, 1.0e-5f );

    const MaterialProjection::TileVariation negative = MaterialProjection::computeTileVariation( 3, -2, 7u );
    SW_EXPECT_NEAR( 0.292734f, negative._offset._x, 1.0e-5f );
    SW_EXPECT_NEAR( 0.483700f, negative._offset._y, 1.0e-5f );
    SW_EXPECT_EQUAL( 3u, negative._rotation );
    SW_EXPECT_NEAR( 0.657102f, negative._brightness, 1.0e-5f );

    SW_EXPECT_TRUE( MaterialProjection::hashTile( 1, 2, 0u ) != MaterialProjection::hashTile( 2, 1, 0u ) );
    SW_EXPECT_TRUE( MaterialProjection::hashTile( 1, 2, 0u ) != MaterialProjection::hashTile( 1, 2, 1u ) );

    uint32  arrRotationCount[4]{};
    float32 brightnessSum = 0.0f;
    for ( int32 tileY = -8; tileY < 8; ++tileY )
    {
        for ( int32 tileX = -8; tileX < 8; ++tileX )
        {
            const MaterialProjection::TileVariation variation = MaterialProjection::computeTileVariation( tileX, tileY, 11u );
            SW_EXPECT_TRUE( variation._offset._x >= 0.0f && variation._offset._x < 1.0f );
            SW_EXPECT_TRUE( variation._offset._y >= 0.0f && variation._offset._y < 1.0f );
            SW_EXPECT_TRUE( variation._rotation < 4u );
            ++arrRotationCount[variation._rotation];
            brightnessSum += variation._brightness;
        }
    }
    for ( const uint32 count : arrRotationCount )
    {
        SW_EXPECT_TRUE( count > 32u ); // 256 칸에 넷이면 평균 64 — 한쪽으로 몰리지 않는다
    }
    SW_EXPECT_NEAR( 0.5f, brightnessSum / 256.0f, 0.08f );
}

/**
 * @brief [MaterialProjectionTest] 3 축 면 UV 는 여섯 면 모두 거울상이 아니다 — 면을 바깥에서 볼 때 화면 오른쪽으로 가면 u 가 늘고, 화면 위로 가면 v 가 준다
 * @details 왼손 좌표계라 오른쪽 = 화면 위 × 시선(시선 = -N)이다. 화면 위는 벽이 +y, 바닥 · 천장이 +z 다. 변이: 면 부호를 빼면 -x · +z · -y 면 중 하나가 진다.
 */
SW_TEST_CASE( MaterialProjectionTest, TriplanarFaceUVIsNeverMirrored )
{
    const float3 arrNormal[] = {
        float3{ 1.0f,  0.0f,  0.0f},
        float3{-1.0f,  0.0f,  0.0f},
        float3{ 0.0f,  1.0f,  0.0f},
        float3{ 0.0f, -1.0f,  0.0f},
        float3{ 0.0f,  0.0f,  1.0f},
        float3{ 0.0f,  0.0f, -1.0f}
    };
    const uint32 arrAxis[] = { 0, 0, 1, 1, 2, 2 };
    const float3 origin{ 0.3f, -0.7f, 1.1f };
    for ( uint32 face = 0; face < 6; ++face )
    {
        const float3& normal   = arrNormal[face];
        const float3  screenUp = arrAxis[face] == 1 ? float3{ 0.0f, 0.0f, 1.0f } : float3{ 0.0f, 1.0f, 0.0f };
        const float3  right    = screenUp.cross( -normal );
        const float2  base     = MaterialProjection::computeTriplanarFaceUV( origin, normal, arrAxis[face] );
        const float2  toRight  = MaterialProjection::computeTriplanarFaceUV( origin + right, normal, arrAxis[face] );
        const float2  toUp     = MaterialProjection::computeTriplanarFaceUV( origin + screenUp, normal, arrAxis[face] );
        SW_EXPECT_NEAR( 1.0f, toRight._x - base._x, 1.0e-5f );
        SW_EXPECT_NEAR( 0.0f, toRight._y - base._y, 1.0e-5f );
        SW_EXPECT_NEAR( -1.0f, toUp._y - base._y, 1.0e-5f );
        SW_EXPECT_NEAR( 0.0f, toUp._x - base._x, 1.0e-5f );
    }
}
