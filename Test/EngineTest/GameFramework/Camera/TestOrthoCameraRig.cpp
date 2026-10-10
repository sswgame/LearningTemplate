#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Camera/OrthoCameraRigComponent.h"

#include "TestFramework/TestFramework.h"

// 비스듬히 내려다보는 직교 카메라 리그 — 시점 계산 · 화면 기준 이동 · 확대 범위 · 외부 시점 덮어쓰기.

using namespace sw;

namespace
{
    struct OrthoCameraRigTestInternal
    {
        static constexpr float32 kPitch       = 55.0f * MathUtil::kDegreeToRadian;
        static constexpr float32 kOrthoHeight = 34.0f;
        static constexpr float32 kAspect      = 16.0f / 9.0f;

        /** @brief 화면 점의 광선이 땅(y = 0)에 닿는 점입니다. */
        static float3 hitGround( const float3& focus, float32 yaw, const float2& mouse )
        {
            const GameRay ray      = OrthoCameraRigMath::computeScreenRay( focus, yaw, kPitch, 120.0f, kOrthoHeight, kAspect, mouse );
            float32       distance = 0.0f;
            SW_EXPECT_TRUE( RayMath::intersectHorizontalPlane( ray, 0.0f, 1000.0f, distance ) );
            return ray._origin + ray._direction * distance;
        }
    };
} // namespace

/**
 * @brief [OrthoCameraRigTest] 리그가 놓은 카메라는 어느 요 · 피치에서든 초점을 정면으로 보고, 초점에서 거리만큼 떨어져 있다
 * @details 오일러 → 쿼터니언(엔진의 `makeFromYawPitchRoll`)으로 +Z 를 돌려 본 방향이 초점 쪽과 같은지 본다 — 계산식을 다시 쓰지 않고
 *          엔진의 회전 규칙으로 대조한다.
 */
SW_TEST_CASE( OrthoCameraRigTest, ViewLooksAtTheFocusFromItsDistance )
{
    const float3 focus{ 20.0f, 0.0f, 20.0f };
    for ( int32 quarter = 0; quarter < 4; ++quarter )
    {
        const float32         yaw   = ( 45.0f + 90.0f * static_cast<float32>( quarter ) ) * MathUtil::kDegreeToRadian;
        const float32         pitch = 30.0f * MathUtil::kDegreeToRadian;
        const OrthoCameraView view  = OrthoCameraRigMath::computeView( focus, yaw, pitch, 250.0f );

        const float3 toFocus = focus - view._position;
        SW_EXPECT_NEAR_EQUAL( 250.0f, toFocus.getLength(), 1.0e-2f );
        const float3 lookDirection = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, quaternion::makeFromYawPitchRoll( view._euler ) );
        const float3 expected      = toFocus * ( 1.0f / toFocus.getLength() );
        SW_EXPECT_NEAR_EQUAL( expected._x, lookDirection._x, 1.0e-4f );
        SW_EXPECT_NEAR_EQUAL( expected._y, lookDirection._y, 1.0e-4f );
        SW_EXPECT_NEAR_EQUAL( expected._z, lookDirection._z, 1.0e-4f );
        SW_EXPECT_NEAR_EQUAL( 125.0f, view._position._y, 1.0e-2f ); // 30° 위 — sin 30° × 250
    }
}

/**
 * @brief [OrthoCameraRigTest] 이동은 화면 기준이다 — 요 0 이면 앞이 +Z · 오른쪽이 +X, 90° 돌면 앞이 +X 다
 */
SW_TEST_CASE( OrthoCameraRigTest, PanFollowsTheYaw )
{
    const float3 forwardAtZero = OrthoCameraRigMath::computePanDirection( 0.0f, 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, forwardAtZero._z, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, forwardAtZero._x, 1.0e-5f );
    const float3 rightAtZero = OrthoCameraRigMath::computePanDirection( 0.0f, 0.0f, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, rightAtZero._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, rightAtZero._z, 1.0e-5f );
    const float3 forwardAtQuarter = OrthoCameraRigMath::computePanDirection( MathUtil::kHalfPi, 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, forwardAtQuarter._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, forwardAtQuarter._z, 1.0e-5f );
    const float3 backLeft = OrthoCameraRigMath::computePanDirection( 0.0f, -1.0f, -1.0f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, backLeft._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, backLeft._z, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, backLeft._y, 1.0e-5f );
}

/**
 * @brief [OrthoCameraRigTest] 휠은 위로 굴리면 확대(높이 × 단계), 아래로 굴리면 축소이고, 범위 밖으로 나가지 않는다
 */
SW_TEST_CASE( OrthoCameraRigTest, ZoomStepsAndStaysInRange )
{
    SW_EXPECT_NEAR_EQUAL( 85.0f, OrthoCameraRigMath::computeZoomedHeight( 100.0f, 1.0f, 0.85f, 25.0f, 220.0f ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f / 0.85f, OrthoCameraRigMath::computeZoomedHeight( 100.0f, -1.0f, 0.85f, 25.0f, 220.0f ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, OrthoCameraRigMath::computeZoomedHeight( 100.0f, 0.0f, 0.85f, 25.0f, 220.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 25.0f, OrthoCameraRigMath::computeZoomedHeight( 26.0f, 1.0f, 0.85f, 25.0f, 220.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 220.0f, OrthoCameraRigMath::computeZoomedHeight( 210.0f, -1.0f, 0.85f, 25.0f, 220.0f ), 1.0e-6f );
}

/**
 * @brief [OrthoCameraRigTest] 리그는 같은 오브젝트의 카메라를 직교로 두고, 덮어쓴 시점이 있으면 그 원근 시점을 쓰며, 풀면 직교로 돌아간다
 */
SW_TEST_CASE( OrthoCameraRigTest, RigDrivesItsCameraAndHonoursTheOverride )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "RigCamera" ) );
    SW_ASSERT_NOT_NULL( pObject );
    CameraComponent*         pCamera = pObject->addComponent<CameraComponent>();
    OrthoCameraRigComponent* pRig    = pObject->addComponent<OrthoCameraRigComponent>();
    SW_ASSERT_TRUE( pCamera != nullptr && pRig != nullptr );

    pRig->setFocus( float3{ 20.0f, 0.0f, 20.0f } );
    pRig->applyToCamera();
    SW_EXPECT_TRUE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( pRig->getOrthoHeight(), pCamera->getOrthoHeight(), 1.0e-5f );
    const OrthoCameraView expected = OrthoCameraRigMath::computeView( float3{ 20.0f, 0.0f, 20.0f }, 45.0f * MathUtil::kDegreeToRadian, 30.0f * MathUtil::kDegreeToRadian, 250.0f );
    SW_EXPECT_NEAR_EQUAL( expected._position._x, pCamera->getLocalPosition()._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( expected._position._y, pCamera->getLocalPosition()._y, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( expected._position._z, pCamera->getLocalPosition()._z, 1.0e-3f );

    // 덮어쓰기는 블렌드로 들어간다 — 켠 순간은 아직 직교 시점이고, 블렌드 길이가 지나면 덮어쓴 시점이다.
    pRig->setViewOverride( float3{ 1.0f, 2.0f, 3.0f }, float3{ 0.0f, 0.5f, 0.25f }, 1.2f, 600.0f );
    pRig->applyToCamera();
    SW_EXPECT_TRUE( pCamera->isOrthographic() );
    pRig->updateCamera( 1.0f );
    SW_EXPECT_FALSE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( 1.2f, pCamera->getFieldOfViewY(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 600.0f, pCamera->getFarPlane(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pCamera->getLocalPosition()._y, 1.0e-5f );

    pRig->clearViewOverride();
    pRig->updateCamera( 1.0f );
    SW_EXPECT_TRUE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( expected._position._y, pCamera->getLocalPosition()._y, 1.0e-3f );
}

/**
 * @brief [OrthoCameraRigTest] 초점 묶기는 X · Z 만 범위 안에 넣고 Y 는 그대로 둔다
 */
SW_TEST_CASE( OrthoCameraRigTest, ClampFocusKeepsXzInsideTheBounds )
{
    const float3 focusMin{ 0.0f, 0.0f, 0.0f };
    const float3 focusMax{ 48.0f, 0.0f, 40.0f };
    const float3 inside = OrthoCameraRigMath::clampFocus( float3{ 10.0f, 3.0f, 20.0f }, focusMin, focusMax );
    SW_EXPECT_NEAR_EQUAL( 10.0f, inside._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, inside._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, inside._z, 1.0e-6f );
    const float3 outside = OrthoCameraRigMath::clampFocus( float3{ -5.0f, 7.0f, 99.0f }, focusMin, focusMax );
    SW_EXPECT_NEAR_EQUAL( 0.0f, outside._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, outside._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 40.0f, outside._z, 1.0e-6f );
}

/**
 * @brief [OrthoCameraRigTest] 화면 가운데의 광선은 초점에 닿고, 오른쪽 끝은 화면 반너비만큼 오른쪽, 위쪽 끝은 반높이 ÷ sin(피치)만큼 앞의 땅에 닿는다
 * @details 요 0(앞이 +Z)에서 화면 위쪽으로 h 옮긴 광선은 땅에서 h / sin(피치) 만큼 멀리 닿는다(위 벡터 (0, cos p, sin p) · 앞 (0, −sin p, cos p) 로 손으로 푼 값).
 *          요 90° 에서도 가운데는 초점이다.
 */
SW_TEST_CASE( OrthoCameraRigTest, ScreenRayHitsTheGroundUnderTheScreenPoint )
{
    using Internal = OrthoCameraRigTestInternal;
    const float3  focus{ 26.0f, 0.0f, 14.0f };
    const float32 halfHeight = Internal::kOrthoHeight * 0.5f;

    const float3 center = Internal::hitGround( focus, 0.0f, float2{ 0.5f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( focus._x, center._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( focus._z, center._z, 1.0e-3f );
    const float3 rightEdge = Internal::hitGround( focus, 0.0f, float2{ 1.0f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( focus._x + halfHeight * Internal::kAspect, rightEdge._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( focus._z, rightEdge._z, 1.0e-3f );
    const float3 topEdge = Internal::hitGround( focus, 0.0f, float2{ 0.5f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( focus._x, topEdge._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( focus._z + halfHeight / MathUtil::sin( Internal::kPitch ), topEdge._z, 1.0e-3f );
    const float3 turnedCenter = Internal::hitGround( focus, MathUtil::kHalfPi, float2{ 0.5f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( focus._x, turnedCenter._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( focus._z, turnedCenter._z, 1.0e-3f );
    const float3 turnedRight = Internal::hitGround( focus, MathUtil::kHalfPi, float2{ 1.0f, 0.5f } ); // 앞이 +X 면 오른쪽은 −Z
    SW_EXPECT_NEAR_EQUAL( focus._z - halfHeight * Internal::kAspect, turnedRight._z, 1.0e-3f );
}

/**
 * @brief [OrthoCameraRigTest] 컴포넌트의 땅 고르기는 자기 초점 · 시점을 쓴다 — 화면 가운데는 초점이다
 */
SW_TEST_CASE( OrthoCameraRigTest, FindGroundPointUsesTheRigView )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "RigCamera" ) );
    SW_ASSERT_NOT_NULL( pObject );
    OrthoCameraRigComponent* pRig = pObject->addComponent<OrthoCameraRigComponent>();
    SW_ASSERT_NOT_NULL( pRig );
    pRig->setFocus( float3{ 32.0f, 0.0f, 30.0f } );
    float3 point{};
    SW_ASSERT_TRUE( pRig->findGroundPoint( float2{ 0.5f, 0.5f }, 16.0f / 9.0f, 0.0f, point ) );
    SW_EXPECT_NEAR_EQUAL( 32.0f, point._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, point._y, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, point._z, 1.0e-3f );
}

/**
 * @brief [OrthoCameraRigTest] 코드가 넣는 화면 높이도 휠과 같은 확대 범위 안으로 묶인다
 */
SW_TEST_CASE( OrthoCameraRigTest, SetOrthoHeightStaysInTheZoomRange )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "RigCamera" ) );
    SW_ASSERT_NOT_NULL( pObject );
    OrthoCameraRigComponent* pRig = pObject->addComponent<OrthoCameraRigComponent>();
    SW_ASSERT_NOT_NULL( pRig );
    pRig->setOrthoHeight( 70.0f );
    SW_EXPECT_NEAR_EQUAL( 70.0f, pRig->getOrthoHeight(), 1.0e-6f );
    pRig->setOrthoHeight( 1000.0f );
    SW_EXPECT_NEAR_EQUAL( 220.0f, pRig->getOrthoHeight(), 1.0e-6f ); // 기본 범위 25 ~ 220
    pRig->setOrthoHeight( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 25.0f, pRig->getOrthoHeight(), 1.0e-6f );
}
