#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Components/FirstPersonCameraComponent.h"

#include "TestFramework/TestFramework.h"

// 1인칭 카메라 — 뷰 모델 자리 · 마우스 시점과 피치 한계 · 컴포넌트가 같은 오브젝트의 카메라와 뷰 모델을 두는 것.

using namespace sw;

/**
 * @brief [FirstPersonCameraTest] 뷰 모델 자리는 눈 기준 오른쪽 · 위 · 앞 오프셋이고, 시점을 돌려도 눈에서의 거리와 앞 성분이 그대로다
 * @details 요 0 이면 오른쪽 +X · 위 +Y · 앞 +Z 다. 요 90° 면 앞이 +X · 오른쪽이 −Z 다. 피치가 있어도 위는 시점의 앞과 수직이다.
 */
SW_TEST_CASE( FirstPersonCameraTest, ViewModelSitsAtTheOffsetFromTheEye )
{
    const float3 eye{ 1.0f, 1.6f, -3.0f };
    const float3 offset{ 0.16f, -0.15f, 0.42f };

    FirstPersonLook look;
    look.setAngles( 0.0f, 0.0f );
    const float3 straight = FirstPersonCameraMath::computeViewModelPosition( eye, look, offset );
    SW_EXPECT_NEAR_EQUAL( eye._x + 0.16f, straight._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( eye._y - 0.15f, straight._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( eye._z + 0.42f, straight._z, 1.0e-5f );

    look.setAngles( MathUtil::HalfPi, 0.0f );
    const float3 turned = FirstPersonCameraMath::computeViewModelPosition( eye, look, offset );
    SW_EXPECT_NEAR_EQUAL( eye._x + 0.42f, turned._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( eye._z - 0.16f, turned._z, 1.0e-5f );

    look.setAngles( 0.7f, 0.5f );
    const float3 toModel = FirstPersonCameraMath::computeViewModelPosition( eye, look, offset ) - eye;
    SW_EXPECT_NEAR_EQUAL( offset.getLength(), toModel.getLength(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( offset._z, toModel.dot( look.getForward() ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( offset._x, toModel.dot( look.getFlatRight() ), 1.0e-4f );
}

/**
 * @brief [FirstPersonCameraTest] 마우스를 오른쪽으로 끌면 요가 감도만큼 늘고, 아래로 끌면 아래를 보며, 피치는 한계에서 멈춘다
 */
SW_TEST_CASE( FirstPersonCameraTest, MouseTurnsAndPitchStopsAtTheLimit )
{
    FirstPersonLook look;
    look.setAngles( 0.0f, 0.0f );
    const FirstPersonLook right = FirstPersonCameraMath::computeLookAfterMouse( look, 100.0f, 0.0f, 0.002f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, right.getYaw(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, right.getPitch(), 1.0e-6f );

    const FirstPersonLook down = FirstPersonCameraMath::computeLookAfterMouse( look, 0.0f, 50.0f, 0.002f );
    SW_EXPECT_NEAR_EQUAL( -0.1f, down.getPitch(), 1.0e-5f );

    look.setMaxPitch( 1.0f );
    const FirstPersonLook floor = FirstPersonCameraMath::computeLookAfterMouse( look, 0.0f, 100000.0f, 0.002f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, floor.getPitch(), 1.0e-6f );
    const FirstPersonLook sky = FirstPersonCameraMath::computeLookAfterMouse( look, 0.0f, -100000.0f, 0.002f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sky.getPitch(), 1.0e-6f );
}

/**
 * @brief [FirstPersonCameraTest] 컴포넌트는 같은 오브젝트의 카메라를 눈 자리 · 시점의 원근으로 두고, 이름이 맞는 메시를 뷰 모델 자리에 둔다
 */
SW_TEST_CASE( FirstPersonCameraTest, ComponentDrivesItsCameraAndViewModel )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Player" ) );
    SW_ASSERT_NOT_NULL( pObject );
    CameraComponent* pCamera    = pObject->addComponent<CameraComponent>();
    MeshComponent*   pOther     = pObject->addComponent<MeshComponent>(); // 이름이 다른 메시가 먼저 붙어 있다
    MeshComponent*   pViewModel = pObject->addComponent<MeshComponent>();
    SW_ASSERT_TRUE( pCamera != nullptr && pViewModel != nullptr && pOther != nullptr );
    pViewModel->setComponentName( hashed_string( "ViewWeapon" ) );
    pOther->setComponentName( hashed_string( "Body" ) );
    FirstPersonCameraComponent* pRig = pObject->addComponent<FirstPersonCameraComponent>();
    SW_ASSERT_NOT_NULL( pRig );

    const float3 eye{ 2.0f, 1.6f, -16.0f };
    const float3 offset{ 0.16f, -0.15f, 0.42f };
    pRig->setViewModel( hashed_string( "ViewWeapon" ), offset, MathUtil::Pi );
    pRig->setAngles( 0.4f, -0.2f );
    pRig->setEyePosition( eye );

    SW_EXPECT_FALSE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( eye._x, pCamera->getLocalPosition()._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( eye._y, pCamera->getLocalPosition()._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( eye._z, pCamera->getLocalPosition()._z, 1.0e-5f );
    const float3 euler = pRig->getLook().computeCameraEuler();
    SW_EXPECT_NEAR_EQUAL( euler._x, pCamera->getLocalRotation()._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( euler._y, pCamera->getLocalRotation()._y, 1.0e-5f );

    // 뷰 모델은 카메라의 자식으로 로컬 오프셋에 놓인다 — 월드에서는 눈 기준 오프셋 자리이고, 총구(-Z)가 시점의 앞(피치까지)을 본다.
    SW_EXPECT_TRUE( pViewModel->getParent() == pCamera );
    SW_EXPECT_NEAR_EQUAL( offset._z, pViewModel->getLocalPosition()._z, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::Pi, pViewModel->getLocalRotation()._y, 1.0e-6f );
    const float4x4 world    = pViewModel->getWorldMatrix();
    const float3   expected = FirstPersonCameraMath::computeViewModelPosition( eye, pRig->getLook(), offset );
    SW_EXPECT_NEAR_EQUAL( expected._x, world.getTranslation()._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( expected._y, world.getTranslation()._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( expected._z, world.getTranslation()._z, 1.0e-4f );
    float3       muzzle  = float3::transformVector( float3{ 0.0f, 0.0f, -1.0f }, world );
    const float3 forward = pRig->getLook().getForward();
    muzzle               = muzzle * ( 1.0f / muzzle.getLength() );
    SW_EXPECT_NEAR_EQUAL( forward._x, muzzle._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( forward._y, muzzle._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( forward._z, muzzle._z, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pOther->getLocalPosition()._x, 1.0e-6f ); // 이름이 다른 메시는 그대로

    // 마우스로 돌리면 카메라도 다음에 둘 때 따라간다.
    const float32 yawBefore = pRig->getLook().getYaw();
    pRig->addMouseDelta( 50.0f, 0.0f );
    pRig->applyToCamera();
    SW_EXPECT_TRUE( pRig->getLook().getYaw() > yawBefore );
    SW_EXPECT_NEAR_EQUAL( pRig->getLook().getYaw(), pCamera->getLocalRotation()._y, 1.0e-5f );
}
