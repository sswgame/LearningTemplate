#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Skeletal/DualQuaternion.h"

using namespace sw;

/**
 * @brief [DualQuaternionTest] 본 행렬 하나를 듀얼 쿼터니언 + 스케일로 나눠 옮긴 점이 행렬로 옮긴 점과 같다
 * @details DQ 스키닝(GPUSceneBuilder 의 팔레트 · mesh_skin.hlsl)은 본마다 `fromMatrix( 행렬, 스케일 )` 을 싣고 `스케일 → 회전 → 이동` 으로 옮긴다.
 *          가중치 1 인 정점에서 선형 스키닝과 같은 자리여야 방식을 바꿔도 바인드 포즈 · 강체 포즈가 그대로다. 균등 스케일 · 거울(음수 X)도 본다.
 */
SW_TEST_CASE( DualQuaternionTest, SingleBoneMatchesMatrix )
{
    const quaternion rotation   = quaternion::makeFromYawPitchRoll( 0.7f, -0.4f, 1.1f );
    const float3     arrScale[] = {
        float3{ 1.0f, 1.0f, 1.0f},
        float3{ 1.5f, 1.5f, 1.5f},
        float3{-2.0f, 2.0f, 2.0f}
    };
    const float3 point{ 0.3f, -1.2f, 2.5f };
    for ( const float3& scaleIn : arrScale )
    {
        const float4x4       matrix = float4x4::makeScale( scaleIn ) * float4x4::makeFromQuaternion( rotation ) * float4x4::makeTranslation( 4.0f, -1.0f, 0.5f );
        float3               scale{};
        const DualQuaternion bone     = DualQuaternion::fromMatrix( matrix, scale );
        const float3         scaled   = float3{ point._x * scale._x, point._y * scale._y, point._z * scale._z };
        const float3         expected = float3::transform( point, matrix );
        const float3         actual   = bone.transformPoint( scaled );
        SW_EXPECT_NEAR( expected._x, actual._x, 1e-4f );
        SW_EXPECT_NEAR( expected._y, actual._y, 1e-4f );
        SW_EXPECT_NEAR( expected._z, actual._z, 1e-4f );
    }
}

/**
 * @brief [DualQuaternionTest] 180 도 비튼 두 본을 반씩 섞으면 DQ 는 축에서 거리를 지키고 선형은 축으로 무너진다(캔디 래퍼)
 * @details mesh_skin.hlsl 의 DQ 경로는 첫 본에 부호를 맞춘 가중 합 → 정규화다. 두 본이면 `DualQuaternion::dlb` 와 같은 식이다.
 */
SW_TEST_CASE( DualQuaternionTest, TwistKeepsVolume )
{
    const float4x4       twisted = float4x4::makeRotationX( 3.14159265f );
    const DualQuaternion rest{};
    const DualQuaternion twist = DualQuaternion::fromMatrix( twisted );
    const float3         point{ 0.0f, 1.0f, 0.0f };

    const float3 linear = ( float3::transform( point, float4x4::Identity ) + float3::transform( point, twisted ) ) * 0.5f;
    SW_EXPECT_NEAR( 0.0f, linear.getLength(), 1e-4f );

    const float3 blended = DualQuaternion::dlb( rest, twist, 0.5f ).transformPoint( point );
    SW_EXPECT_NEAR( 1.0f, blended.getLength(), 1e-4f );
    SW_EXPECT_NEAR( 0.0f, blended._x, 1e-4f );
}
