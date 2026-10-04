#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Components/OrthoCameraRigComponent.h"

#include "TestFramework/TestFramework.h"

// 비스듬히 내려다보는 직교 카메라 리그 — 시점 계산 · 화면 기준 이동 · 확대 범위 · 외부 시점 덮어쓰기.

using namespace sw;

/**
 * @brief [OrthoCameraRigTest] 리그가 놓은 카메라는 어느 요 · 피치에서든 초점을 정면으로 보고, 초점에서 거리만큼 떨어져 있다
 * @details 오일러 → 쿼터니언(엔진의 `createFromYawPitchRoll`)으로 +Z 를 돌려 본 방향이 초점 쪽과 같은지 본다 — 계산식을 다시 쓰지 않고
 *          엔진의 회전 규칙으로 대조한다.
 */
SW_TEST_CASE( OrthoCameraRigTest, ViewLooksAtTheFocusFromItsDistance )
{
    const float3 focus{ 20.0f, 0.0f, 20.0f };
    for ( int32 quarter = 0; quarter < 4; ++quarter )
    {
        const float32         yaw   = ( 45.0f + 90.0f * static_cast<float32>( quarter ) ) * MathUtil::DegreeToRadian;
        const float32         pitch = 30.0f * MathUtil::DegreeToRadian;
        const OrthoCameraView view  = OrthoCameraRigMath::computeView( focus, yaw, pitch, 250.0f );

        const float3 toFocus = focus - view._position;
        SW_EXPECT_NEAR_EQUAL( 250.0f, toFocus.getLength(), 1.0e-2f );
        const float3 lookDirection = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, quaternion::createFromYawPitchRoll( view._euler ) );
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
    const float3 forwardAtQuarter = OrthoCameraRigMath::computePanDirection( MathUtil::HalfPi, 1.0f, 0.0f );
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
    const OrthoCameraView expected = OrthoCameraRigMath::computeView( float3{ 20.0f, 0.0f, 20.0f }, 45.0f * MathUtil::DegreeToRadian, 30.0f * MathUtil::DegreeToRadian, 250.0f );
    SW_EXPECT_NEAR_EQUAL( expected._position._x, pCamera->getLocalPosition()._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( expected._position._y, pCamera->getLocalPosition()._y, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( expected._position._z, pCamera->getLocalPosition()._z, 1.0e-3f );

    pRig->setViewOverride( float3{ 1.0f, 2.0f, 3.0f }, float3{ 0.0f, 0.5f, 0.25f }, 1.2f, 600.0f );
    pRig->applyToCamera();
    SW_EXPECT_FALSE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( 1.2f, pCamera->getFieldOfViewY(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 600.0f, pCamera->getFarPlane(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pCamera->getLocalPosition()._y, 1.0e-5f );

    pRig->clearViewOverride();
    pRig->applyToCamera();
    SW_EXPECT_TRUE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( expected._position._y, pCamera->getLocalPosition()._y, 1.0e-3f );
}
