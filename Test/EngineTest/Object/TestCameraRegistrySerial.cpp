#include "pch.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// CameraRegistrySerialTest — 카메라 레지스트리 변경 일련번호: 등록 · 해제 · 역할 · 우선순위 · 출력 대상이 실제로 바뀔 때만 오른다. 장면 없이 매니저만 쓴다(nogpu).

/**
 * @brief [CameraRegistrySerialTest] 등록과 해제는 번호를 올리고, 같은 값으로 다시 쓰는 세터는 올리지 않는다
 */
SW_TEST_CASE( CameraRegistrySerialTest, RegisterUnregisterAndSettersBumpOnlyOnRealChange )
{
    GameObjectManager     manager;
    const CameraRegistry& registry = manager.getCameraRegistry();
    const uint32          initial  = registry.getRegistrySerial();

    GameObject* pObject = manager.createGameObject( hashed_string( "SerialCamera" ) );
    SW_ASSERT_NOT_NULL( pObject );
    CameraComponent* pCamera = pObject->addComponent<CameraComponent>();
    SW_ASSERT_NOT_NULL( pCamera );
    const uint32 afterAdd = registry.getRegistrySerial();
    SW_EXPECT_TRUE( afterAdd != initial );

    // 같은 카메라를 다시 등록해도 오르지 않는다(이미 있음).
    manager.getCameraRegistry().add( pCamera );
    SW_EXPECT_EQUAL( afterAdd, registry.getRegistrySerial() );

    // 역할: 바뀔 때만 오른다.
    const CameraRole sameRole = pCamera->getRole();
    pCamera->setRole( sameRole );
    SW_EXPECT_EQUAL( afterAdd, registry.getRegistrySerial() );
    pCamera->setRole( sameRole == CameraRole::Capture ? CameraRole::Game : CameraRole::Capture );
    const uint32 afterRole = registry.getRegistrySerial();
    SW_EXPECT_TRUE( afterRole != afterAdd );

    // 우선순위.
    pCamera->setPriority( pCamera->getPriority() );
    SW_EXPECT_EQUAL( afterRole, registry.getRegistrySerial() );
    pCamera->setPriority( pCamera->getPriority() + 1 );
    const uint32 afterPriority = registry.getRegistrySerial();
    SW_EXPECT_TRUE( afterPriority != afterRole );

    // 출력 대상: 대상이 같으면 다른 필드가 바뀌어도 오르지 않는다.
    CameraRenderOutput output = pCamera->getRenderOutput();
    output._screenRect        = float4{ 0.0f, 0.0f, 0.5f, 1.0f };
    pCamera->setRenderOutput( output );
    SW_EXPECT_EQUAL( afterPriority, registry.getRegistrySerial() );
    output._target = ( output._target == CameraOutputTarget::MainView ) ? CameraOutputTarget::ScreenRect : CameraOutputTarget::MainView;
    pCamera->setRenderOutput( output );
    SW_EXPECT_TRUE( registry.getRegistrySerial() != afterPriority );

    // 해제도 오르고, 이미 없는 카메라를 또 빼도 오르지 않는다.
    const uint32 beforeRemove = registry.getRegistrySerial();
    manager.getCameraRegistry().remove( pCamera );
    const uint32 afterRemove = registry.getRegistrySerial();
    SW_EXPECT_TRUE( afterRemove != beforeRemove );
    manager.getCameraRegistry().remove( pCamera );
    SW_EXPECT_EQUAL( afterRemove, registry.getRegistrySerial() );
}
