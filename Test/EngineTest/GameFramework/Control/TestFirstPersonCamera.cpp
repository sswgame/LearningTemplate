#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Control/FirstPersonCameraComponent.h"

#include "TestFramework/TestFramework.h"

// 1인칭 카메라 — 뷰 모델 자리 · 컴포넌트가 같은 오브젝트의 카메라와 뷰 모델을 두는 것(시점 = 폰의 조종 회전은 ControlTest).

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

    look.setAngles( MathUtil::kHalfPi, 0.0f );
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
    pRig->setViewModel( hashed_string( "ViewWeapon" ), offset, MathUtil::kPi );
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
    SW_EXPECT_NEAR_EQUAL( MathUtil::kPi, pViewModel->getLocalRotation()._y, 1.0e-6f );
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

    // 폰이 없는 카메라(관전)는 코드가 정한 시점을 지킨다 — 틱이 지나도 그대로, 카메라도 따라간다.
    pRig->setAngles( 0.9f, 0.0f );
    pRig->onTick( 1.0f / 60.0f );
    SW_EXPECT_NEAR_EQUAL( 0.9f, pRig->getLook().getYaw(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( pRig->getLook().getYaw(), pCamera->getLocalRotation()._y, 1.0e-5f );
}

/**
 * @brief [FirstPersonCameraTest] 눈 자리는 카메라의 부모 공간이다 — 부모(몸)를 옮기고 돌리면 카메라는 부모 × 눈 자리, 손에 든 모델은 그 카메라 앞 오프셋에 있다
 */
SW_TEST_CASE( FirstPersonCameraTest, CameraFollowsItsParentWhenTheEyeIsLocal )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Rider" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SceneComponent*  pBody      = pObject->addComponent<SceneComponent>(); // 첫 씬 컴포넌트 — 카메라 · 손에 든 모델의 부모
    CameraComponent* pCamera    = pObject->addComponent<CameraComponent>();
    MeshComponent*   pViewModel = pObject->addComponent<MeshComponent>();
    SW_ASSERT_TRUE( pBody != nullptr && pCamera != nullptr && pViewModel != nullptr );
    pViewModel->setComponentName( hashed_string( "ViewWeapon" ) );
    FirstPersonCameraComponent* pRig = pObject->addComponent<FirstPersonCameraComponent>();
    SW_ASSERT_NOT_NULL( pRig );
    if ( pCamera->getParent() != pBody )
        SW_ASSERT_TRUE( pCamera->attachToComponent( pBody, AttachRule::KeepRelative ) );
    manager.beginPlay();

    const float3 offset{ 0.16f, -0.15f, 0.42f };
    pRig->setViewModel( hashed_string( "ViewWeapon" ), offset, MathUtil::kPi );
    pBody->setLocalPosition( float3{ 5.0f, 0.0f, 3.0f } );
    pBody->setLocalRotation( float3{ 0.0f, MathUtil::kHalfPi, 0.0f } ); // 몸이 +X 를 본다
    const float3 eye{ 0.0f, 1.6f, 0.2f };
    pRig->setAngles( 0.0f, 0.0f );
    pRig->setEyePosition( eye );

    // 카메라 월드 = 몸 월드 × 눈 자리 — 눈의 앞 0.2 는 몸이 돈 쪽(+X)으로 간다.
    const float3 expectedEye = float3::transform( eye, pBody->getWorldMatrix() );
    SW_EXPECT_NEAR_EQUAL( expectedEye._x, pCamera->getWorldPosition()._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( expectedEye._y, pCamera->getWorldPosition()._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( expectedEye._z, pCamera->getWorldPosition()._z, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.2f, pCamera->getWorldPosition()._x, 1.0e-4f );
    // 손에 든 모델 = 카메라 월드 × 오프셋, 총구(-Z)는 카메라 앞(+X)을 본다.
    const float4x4 viewModelWorld = pViewModel->getWorldMatrix();
    const float3   expectedModel  = float3::transform( offset, pCamera->getWorldMatrix() );
    SW_EXPECT_NEAR_EQUAL( expectedModel._x, viewModelWorld.getTranslation()._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( expectedModel._y, viewModelWorld.getTranslation()._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( expectedModel._z, viewModelWorld.getTranslation()._z, 1.0e-4f );
    float3 muzzle = float3::transformVector( float3{ 0.0f, 0.0f, -1.0f }, viewModelWorld );
    muzzle        = muzzle * ( 1.0f / muzzle.getLength() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, muzzle._x, 1.0e-4f );

    // 몸이 다시 움직이면 다음에 둘 때 카메라도 따라간다.
    pBody->setLocalPosition( float3{ -2.0f, 0.0f, 0.0f } );
    pRig->setEyePosition( eye );
    SW_EXPECT_NEAR_EQUAL( -2.0f + 0.2f, pCamera->getWorldPosition()._x, 1.0e-4f );
    manager.endPlay();
}
