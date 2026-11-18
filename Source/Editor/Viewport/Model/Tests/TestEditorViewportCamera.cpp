// 씬 뷰 카메라 값 계산(Model 쪽) — 초점 거리, 휠 줌 · 궤도 · 직교 축 보기가 보는 점을 지키는지, 높이를 모르는 팬은 움직이지 않는지.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/EditorViewportCamera.h"

namespace sw
{
    namespace
    {
        /** @brief 회전(도, 피치 · 요)의 시선 방향입니다 — 카메라가 쓰는 식과 같다(왼손 좌표계, 요 0 이면 +Z). */
        float3 computeTestForward( const float3& rotation )
        {
            const float32 pitchRad = MathUtil::toRadian( rotation._x );
            const float32 yawRad   = MathUtil::toRadian( rotation._y );
            return float3{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ), MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
        }

        /** @brief 카메라가 보는 점(시선 앞 궤도 거리)입니다. */
        float3 computeLookPoint( const editor::EditorViewportCamera& camera )
        {
            return camera.getPosition() + computeTestForward( camera.getRotation() ) * camera.getOrbitDistance();
        }

        void expectNear( const float3& expected, const float3& actual )
        {
            SW_EXPECT_NEAR( expected._x, actual._x, 1e-3f );
            SW_EXPECT_NEAR( expected._y, actual._y, 1e-3f );
            SW_EXPECT_NEAR( expected._z, actual._z, 1e-3f );
        }
    } // namespace
} // namespace sw

/**
 * @brief [EditorViewportCameraTest] 초점은 반지름에 비례해 물러나고(반지름 × 2.5), 점 같은 물체에도 최소 궤도 거리를 지킨다
 */
SW_TEST_CASE( EditorViewportCameraTest, FocusDistanceFollowsRadius )
{
    sw::editor::EditorViewportCamera camera;
    const sw::float3                 target{ 4.0f, 1.0f, -2.0f };
    camera.focusOnPoint( target, 4.0f );
    SW_EXPECT_NEAR( 10.0f, camera.getOrbitDistance(), 1e-4f );
    sw::expectNear( target, sw::computeLookPoint( camera ) );

    camera.focusOnPoint( target, 0.0f );
    SW_EXPECT_NEAR( 0.5f, camera.getOrbitDistance(), 1e-4f );
    sw::expectNear( target, sw::computeLookPoint( camera ) );
}

/**
 * @brief [EditorViewportCameraTest] 휠 줌 · 궤도 회전 · 직교 축 보기는 지금 보는 점을 그대로 둔다
 */
SW_TEST_CASE( EditorViewportCameraTest, ZoomOrbitAndAxisViewKeepTheLookPoint )
{
    sw::editor::EditorViewportCamera camera;
    camera.setPose( sw::float3{ 1.0f, 5.0f, -8.0f }, sw::float3{ 30.0f, 45.0f, 0.0f } );
    const sw::float3 lookPoint = sw::computeLookPoint( camera );

    camera.dolly( 2.0f );
    sw::expectNear( lookPoint, sw::computeLookPoint( camera ) );

    camera.orbitTo( sw::float3{ 10.0f, -60.0f, 0.0f } );
    sw::expectNear( lookPoint, sw::computeLookPoint( camera ) );

    SW_EXPECT_FALSE( camera.isOrthographicView() );
    camera.setOrthographicView( 90.0f, 0.0f );
    SW_EXPECT_TRUE( camera.isOrthographicView() );
    sw::expectNear( lookPoint, sw::computeLookPoint( camera ) );
}

/**
 * @brief [EditorViewportCameraTest] 뷰포트 높이를 모르는(0) 팬은 카메라를 옮기지 않고, 아는 팬은 위치와 보는 점을 같이 옮긴다
 */
SW_TEST_CASE( EditorViewportCameraTest, PanNeedsViewportHeight )
{
    sw::editor::EditorViewportCamera camera;
    const sw::float3                 before = camera.getPosition();
    camera.pan( 20.0f, 10.0f, 0.0f );
    sw::expectNear( before, camera.getPosition() );

    const sw::float3 lookBefore = sw::computeLookPoint( camera );
    camera.pan( 20.0f, 0.0f, 600.0f );
    const sw::float3 moved = camera.getPosition() - before;
    SW_EXPECT_TRUE( moved.getLength() > 0.0f );
    sw::expectNear( lookBefore + moved, sw::computeLookPoint( camera ) );
}
