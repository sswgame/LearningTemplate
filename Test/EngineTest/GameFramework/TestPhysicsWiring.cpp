#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Graph/BlendCurve.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Camera/CameraCollisionProbe.h"
#include "GameFramework/Base/Gameplay/Gimmick/Genre/PlatformerGimmicks.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/GrabberComponent.h"
#include "GameFramework/Base/World/Query/WorldQuery.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTick.h"

using namespace sw;

// PhysicsWiringTest — 강체 물리와 게임플레이의 이음: 소켓 부착 → 강체(떨어뜨리기 · 되돌아오기), 월드 질의 · 카메라 암이 강체를 본다, 집기 · 던지기,
// 눌림판 무게 = 강체 질량, 캐릭터 컨트롤러의 발사대 · 컨베이어.

namespace
{
    struct TestPhysicsWiringInternal
    {

        static RigidBodyComponent* spawnBox( GameObjectManager& manager, const utf8* pName, const float3& position, const float3& halfExtents, PhysicsBodyType type,
                                             const utf8* pLayer )
        {
            GameObject*         pObject = manager.createGameObject( hashed_string( pName ) );
            RigidBodyComponent* pBody   = pObject != nullptr ? pObject->addComponent<RigidBodyComponent>() : nullptr;
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( type );
            pBody->setLayer( hashed_string( pLayer ) );
            pBody->setLocalPosition( position );
            return pBody;
        }
    };
} // namespace

/**
 * @brief [PhysicsWiringTest] 소켓에 붙은 칼(강체)은 키네마틱으로 손을 따르고, 물리로 떼면 시작 속도로 날아 바닥에 떨어지고, 되돌아가기는 그 자리에서 손으로 섞인다
 */
SW_TEST_CASE( PhysicsWiringTest, SocketReleaseDropsRigidBodyAndReturns )
{
    GameObjectManager manager;
    SW_ASSERT_NOT_NULL( TestPhysicsWiringInternal::spawnBox( manager, "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 20.0f, 0.5f, 20.0f }, PhysicsBodyType::Static, "Static" ) );
    GameObject* pHolder = manager.createGameObject( hashed_string( "Holder" ) );
    SW_ASSERT_NOT_NULL( pHolder );
    SceneComponent* pHolderScene = pHolder->addComponent<SceneComponent>();
    pHolderScene->setLocalPosition( float3{ 0.0f, 0.0f, 0.0f } );
    RigidBodyComponent* pSword = TestPhysicsWiringInternal::spawnBox( manager, "Sword", float3{}, float3{ 0.05f, 0.4f, 0.05f }, PhysicsBodyType::Dynamic, "Debris" );
    SW_ASSERT_NOT_NULL( pSword );
    SocketBindingComponent* pBinding = pSword->getOwner()->addComponent<SocketBindingComponent>();
    SW_ASSERT_NOT_NULL( pBinding );
    BlendCurveDef blend;
    blend._curve    = BlendCurve::Linear;
    blend._duration = 0.5f;
    pBinding->setReturnBlend( blend );

    manager.beginPlay();
    float4x4 socket = float4x4::Identity;
    socket.setTranslation( float3{ 0.3f, 1.5f, 0.0f } );
    SW_ASSERT_TRUE( pBinding->bindToSocket( pHolder, hashed_string( "HandR" ), socket ) );
    test::tickFrames( manager, 30 );
    // 붙은 동안은 키네마틱 — 동적 데이터였어도 떨어지지 않고 손을 따른다.
    SW_EXPECT_TRUE( pSword->getBodyType() == PhysicsBodyType::Kinematic );
    SW_EXPECT_NEAR_EQUAL( 1.5f, pSword->getWorldPosition()._y, 1e-3f );
    pHolderScene->setWorldPosition( float3{ 1.0f, 0.0f, 0.0f } );
    test::tickFrames( manager, 2 );
    SW_EXPECT_NEAR_EQUAL( 1.3f, pSword->getWorldPosition()._x, 1e-3f );

    SW_ASSERT_TRUE( pBinding->release( SocketReleaseMode::Physics, float3{ 2.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( pBinding->getState() == SocketBindingState::ReleasedPhysics );
    test::tickFrames( manager, 120 );
    SW_EXPECT_TRUE( pSword->getBodyType() == PhysicsBodyType::Dynamic );
    const float3 landed = pSword->getWorldPosition();
    SW_EXPECT_TRUE( landed._y < 0.5f ); // 바닥에 떨어졌다
    SW_EXPECT_TRUE( landed._y > 0.0f ); // 바닥 위
    SW_EXPECT_TRUE( landed._x > 1.6f ); // 시작 속도로 +X 로 갔다

    SW_ASSERT_TRUE( pBinding->returnToSocket() );
    test::tickFrames( manager, 40 );
    SW_EXPECT_TRUE( pBinding->getState() == SocketBindingState::Bound );
    SW_EXPECT_TRUE( pSword->getBodyType() == PhysicsBodyType::Kinematic );
    SW_EXPECT_NEAR_EQUAL( 1.3f, pSword->getWorldPosition()._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.5f, pSword->getWorldPosition()._y, 1e-3f );
    manager.endPlay();
}

/**
 * @brief [PhysicsWiringTest] 월드 질의(시야 · 레이저)와 카메라 암 질의가 강체(Jolt 3D · Box2D 2D)를 본다 — 쏘는 쪽의 바디는 건너뛰고, 겹침 월드 콜라이더도 그대로 본다
 */
SW_TEST_CASE( PhysicsWiringTest, WorldQueryAndCameraProbeSeeRigidBodies )
{
    GameObjectManager   manager;
    RigidBodyComponent* pWall   = TestPhysicsWiringInternal::spawnBox( manager, "Wall", float3{ 0.0f, 1.0f, 5.0f }, float3{ 3.0f, 1.0f, 0.2f }, PhysicsBodyType::Static,
                                                                       "Static" );
    RigidBodyComponent* pViewer = TestPhysicsWiringInternal::spawnBox( manager, "Viewer", float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.3f, 0.3f, 0.3f },
                                                                       PhysicsBodyType::Kinematic, "Default" );
    SW_ASSERT_TRUE( pWall != nullptr && pViewer != nullptr );
    GameObject*           pPlatform2D = manager.createGameObject( hashed_string( "Platform2D" ) );
    RigidBody2DComponent* pBody2D     = pPlatform2D->addComponent<RigidBody2DComponent>();
    SW_ASSERT_NOT_NULL( pBody2D );
    PhysicsShapeDesc2D box2D;
    box2D._halfExtents = float2{ 1.0f, 0.2f };
    pBody2D->setShape( box2D );
    pBody2D->setBodyType( PhysicsBodyType::Static );
    pBody2D->setLocalPosition( float3{ 10.0f, 5.0f, 0.0f } );
    manager.beginPlay();
    test::tickFrames( manager, 2 );

    const uint64 viewerID = pViewer->getOwner()->getObjectID();
    WorldRayHit  hit;
    SW_ASSERT_TRUE( WorldQuery::raycast( manager, float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 1.0f, 10.0f }, viewerID, hit ) );
    SW_EXPECT_EQUAL( pWall->getOwner()->getObjectID(), hit._objectID );
    SW_EXPECT_NEAR_EQUAL( 4.8f, hit._point._z, 1e-2f );
    SW_EXPECT_FALSE( WorldQuery::hasLineOfSight( manager, float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 1.0f, 8.0f }, viewerID, 0 ) );
    // 2D 씬(XY 평면)의 바디도 맞는다.
    SW_ASSERT_TRUE( WorldQuery::raycast( manager, float3{ 10.0f, 10.0f, 0.0f }, float3{ 10.0f, 0.0f, 0.0f }, 0, hit ) );
    SW_EXPECT_EQUAL( pPlatform2D->getObjectID(), hit._objectID );
    SW_EXPECT_NEAR_EQUAL( 0.48f, hit._fraction, 1e-2f );

    // 카메라 암 — 강체 벽 앞에서 멈춘다(반지름 0.25 면 4.55 쯤).
    const SceneCameraProbe probe( manager, viewerID );
    float32                distance = 0.0f;
    SW_ASSERT_TRUE( probe.sweepSphere( float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 1.0f, 8.0f }, 0.25f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 4.55f, distance, 2e-2f );
    manager.endPlay();
}

/**
 * @brief [PhysicsWiringTest] 집기 — 강체는 키네마틱으로 손을 따르고, 던지면 동적으로 돌아가 던진 속도로 난다
 */
SW_TEST_CASE( PhysicsWiringTest, GrabberHoldsRigidBodyKinematicAndThrows )
{
    GameObjectManager manager;
    GameObject*       pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    pPlayer->addComponent<SceneComponent>();
    GrabberComponent*   pGrabber = pPlayer->addComponent<GrabberComponent>();
    RigidBodyComponent* pCrate   = TestPhysicsWiringInternal::spawnBox( manager, "Crate", float3{ 0.0f, 0.5f, 1.0f }, float3{ 0.25f, 0.25f, 0.25f },
                                                                        PhysicsBodyType::Dynamic, "Default" );
    SW_ASSERT_TRUE( pGrabber != nullptr && pCrate != nullptr );
    manager.beginPlay();
    test::tickFrames( manager, 2 );
    SW_ASSERT_TRUE( pGrabber->grab( *pCrate->getOwner() ) );
    test::tickFrames( manager, 30 );
    SW_EXPECT_TRUE( pCrate->getBodyType() == PhysicsBodyType::Kinematic );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pCrate->getWorldPosition()._y, 1e-3f ); // 떨어지지 않고 손 높이(hold offset)
    SW_ASSERT_TRUE( pGrabber->throwHeld( float3{ 0.0f, 0.0f, 6.0f } ) );
    test::tickFrames( manager, 10 );
    SW_EXPECT_TRUE( pCrate->getBodyType() == PhysicsBodyType::Dynamic );
    SW_EXPECT_TRUE( pCrate->getWorldPosition()._z > 1.3f ); // 손(z 0.6)에서 6 m/s 로 10 프레임
    manager.endPlay();
}

/**
 * @brief [PhysicsWiringTest] 눌림판 무게 — 데이터 무게가 없으면 강체 질량, 그것도 없으면 센서의 기본 무게
 */
SW_TEST_CASE( PhysicsWiringTest, PressurePlateWeighsRigidBodyMass )
{
    GameObjectManager manager;
    GameObject*       pPlate = manager.createGameObject( hashed_string( "Plate" ) );
    pPlate->addComponent<SceneComponent>();
    GimmickSensorComponent* pSensor = pPlate->addComponent<GimmickSensorComponent>();
    RigidBodyComponent*     pHeavy  = TestPhysicsWiringInternal::spawnBox( manager, "Heavy", float3{ 0.0f, 3.0f, 0.0f }, float3{ 0.5f, 0.5f, 0.5f },
                                                                           PhysicsBodyType::Dynamic, "Default" );
    SW_ASSERT_TRUE( pSensor != nullptr && pHeavy != nullptr );
    pHeavy->setMass( 30.0f );
    GameObject* pPlain = manager.createGameObject( hashed_string( "Plain" ) );
    pPlain->addComponent<SceneComponent>();
    manager.beginPlay();
    test::tickFrames( manager, 2 );
    pSensor->addOccupant( *pHeavy->getOwner() );
    pSensor->addOccupant( *pPlain );
    SW_EXPECT_NEAR_EQUAL( 31.0f, pSensor->computeOccupantWeight(), 1e-3f ); // 30(강체) + 1(기본)
    pHeavy->getOwner()->addComponent<GimmickWeightComponent>()->setWeight( 5.0f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, pSensor->computeOccupantWeight(), 1e-3f ); // 데이터 무게가 이긴다
    manager.endPlay();
}

/**
 * @brief [PhysicsWiringTest] 캐릭터 컨트롤러 — 발사대는 쏜 속도(수직 + 다시 디딜 때까지 수평)를, 컨베이어는 면 속도를 준다(트랜스폼을 직접 옮기지 않아 벽에 막힌다)
 */
SW_TEST_CASE( PhysicsWiringTest, ControllerTakesLaunchAndConveyorVelocity )
{
    GameObjectManager manager;
    SW_ASSERT_NOT_NULL( TestPhysicsWiringInternal::spawnBox( manager, "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 30.0f, 0.5f, 30.0f }, PhysicsBodyType::Static, "Static" ) );
    SW_ASSERT_NOT_NULL( TestPhysicsWiringInternal::spawnBox( manager, "Wall", float3{ 2.0f, 1.0f, -5.0f }, float3{ 0.2f, 1.0f, 2.0f }, PhysicsBodyType::Static, "Static" ) );
    GameObject*                   pHero       = manager.createGameObject( hashed_string( "Hero" ) );
    CharacterControllerComponent* pController = pHero->addComponent<CharacterControllerComponent>();
    SW_ASSERT_NOT_NULL( pController );
    pController->setLocalPosition( float3{ 0.0f, 0.02f, 0.0f } );
    GameObject* pPad = manager.createGameObject( hashed_string( "Pad" ) );
    pPad->addComponent<SceneComponent>();
    LaunchPadComponent* pLaunchPad = pPad->addComponent<LaunchPadComponent>();
    SW_ASSERT_NOT_NULL( pLaunchPad );

    manager.beginPlay();
    test::tickFrames( manager, 10 );
    SW_EXPECT_TRUE( pController->isGrounded() );
    pLaunchPad->launch( *pHero ); // 기본 (0, 12, 0)
    test::tickFrames( manager, 20 );
    SW_EXPECT_TRUE( pController->getWorldPosition()._y > 2.0f );
    test::tickFrames( manager, 150 );
    SW_EXPECT_TRUE( pController->isGrounded() );

    // 컨베이어: 같은 오브젝트의 센서에 든 것을 면 속도 (2, 0, 0) 으로 — 벽(x = 1.8)에서 멈춘다.
    pController->teleportTo( float3{ 0.0f, 0.02f, -5.0f } );
    GameObject* pBelt = manager.createGameObject( hashed_string( "Belt" ) );
    pBelt->addComponent<SceneComponent>();
    GimmickSensorComponent* pSensor   = pBelt->addComponent<GimmickSensorComponent>();
    ConveyorComponent*      pConveyor = pBelt->addComponent<ConveyorComponent>();
    SW_ASSERT_TRUE( pSensor != nullptr && pConveyor != nullptr );
    test::tickFrames( manager, 2 );
    pSensor->addOccupant( *pHero );
    test::tickFrames( manager, 180 );
    const float32 x = pController->getWorldPosition()._x;
    SW_EXPECT_TRUE( x > 1.0f );
    SW_EXPECT_TRUE( x < 1.6f ); // 벽 앞(캡슐 반지름 0.3)에서 막혔다
    manager.endPlay();
}
