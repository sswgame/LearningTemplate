#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 오일러(피치 · 요 · 롤)로 돌린 앞 · 위가 기대와 같은지 봅니다. */
    bool rotatesTo( const float3& euler, const float3& forward, const float3& up )
    {
        const quaternion rotation   = quaternion::makeFromYawPitchRoll( euler._y, euler._x, euler._z );
        const float3     gotForward = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, rotation );
        const float3     gotUp      = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, rotation );
        return float3::getDistance( gotForward, forward ) < 1.0e-3f && float3::getDistance( gotUp, up ) < 1.0e-3f;
    }
} // namespace

/**
 * @brief [OrientationUtilTest] 앞 · 위로 구한 오일러는 엔진 회전(`makeFromYawPitchRoll`)으로 같은 앞 · 위를 낸다 — 롤 · 뒤집힘 · 수직 앞 포함
 * @details 코스터 차량 · 탑승 카메라가 루프에서 뒤집히는 자리다. 카메라 `lookAt` 은 롤이 없어 그 자리를 못 맞춘다.
 */
SW_TEST_CASE( OrientationUtilTest, EulerReproducesForwardAndUp )
{
    const float32 arrYaw[]   = { 0.0f, 0.7f, -2.1f, 3.0f };
    const float32 arrPitch[] = { 0.0f, 0.5f, -1.2f };
    const float32 arrRoll[]  = { 0.0f, 0.9f, -2.5f, 3.1f };
    bool          bAllMatch  = true;
    for ( const float32 yaw : arrYaw )
    {
        for ( const float32 pitch : arrPitch )
        {
            for ( const float32 roll : arrRoll )
            {
                const quaternion rotation = quaternion::makeFromYawPitchRoll( yaw, pitch, roll );
                const float3     forward  = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, rotation );
                const float3     up       = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, rotation );
                bAllMatch                 = bAllMatch && rotatesTo( OrientationUtil::computeEulerFromForwardUp( forward, up ), forward, up );
            }
        }
    }
    SW_EXPECT_TRUE( bAllMatch );

    // 앞이 수직 — 루프의 옆면.
    SW_EXPECT_TRUE( rotatesTo( OrientationUtil::computeEulerFromForwardUp( float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, -1.0f } ), float3{ 0.0f, 1.0f, 0.0f },
                               float3{ 0.0f, 0.0f, -1.0f } ) );
    SW_EXPECT_TRUE( rotatesTo( OrientationUtil::computeEulerFromForwardUp( float3{ 0.0f, -1.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f } ), float3{ 0.0f, -1.0f, 0.0f },
                               float3{ 1.0f, 0.0f, 0.0f } ) );
}
