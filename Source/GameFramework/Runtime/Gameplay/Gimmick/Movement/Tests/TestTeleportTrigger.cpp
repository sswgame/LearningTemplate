// TeleportTriggerTest — 들어온 것을 도착 오브젝트의 자리 · 방향으로 옮기고, 짝 포털은 도착해서 생긴 진입으로 되돌려 보내지 않는다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/TeleportTriggerComponent.h"

using namespace sw;

namespace
{
    struct TestTeleportTriggerInternal
    {
        static TeleportTriggerComponent* addPortal( GameObjectManager& manager, const utf8* pName, const float3& position, float32 yaw )
        {
            GameObject*     pObject = manager.createGameObject( hashed_string( pName ) );
            SceneComponent* pScene  = pObject->addComponent<SceneComponent>();
            pScene->setLocalPosition( position );
            pScene->setLocalRotation( float3{ 0.0f, yaw, 0.0f } );
            return pObject->addComponent<TeleportTriggerComponent>();
        }
    };
} // namespace

/** @brief [TeleportTriggerTest] 짝 포털 A → B: 자리와 몸 요가 B 의 것이 되고, B 는 도착 진입을 세지 않으며, 다시 들어가면 A 로 돌아온다 */
SW_TEST_CASE( TeleportTriggerTest, PairedPortalsMoveOnceAndDoNotBounce )
{
    using Internal = TestTeleportTriggerInternal;
    GameObjectManager         manager;
    TeleportTriggerComponent* pA = Internal::addPortal( manager, "PortalA", float3{ 0.0f, 0.0f, 0.0f }, 0.0f );
    TeleportTriggerComponent* pB = Internal::addPortal( manager, "PortalB", float3{ 10.0f, 2.0f, 0.0f }, MathUtil::kHalfPi );
    pA->setDestination( pB->getOwner()->getHandle() );
    pB->setDestination( pA->getOwner()->getHandle() );
    GameObject*     pRider = manager.createGameObject( hashed_string( "Rider" ) );
    SceneComponent* pRoot  = pRider->addComponent<SceneComponent>();
    pRoot->setLocalPosition( float3{ 0.2f, 0.0f, 0.0f } );
    CharacterMovementComponent* pMovement = pRider->addComponent<CharacterMovementComponent>();

    SW_EXPECT_TRUE( pA->activate( pRider ) );
    SW_EXPECT_NEAR( 10.0f, pRoot->getWorldPosition()._x, 1.0e-4f );
    SW_EXPECT_NEAR( 2.0f, pRoot->getWorldPosition()._y, 1.0e-4f );
    SW_EXPECT_NEAR( MathUtil::kHalfPi, pMovement->getFacingYaw(), 1.0e-3f );

    SW_EXPECT_FALSE( pB->activate( pRider ) ); // 도착해서 생긴 진입
    SW_EXPECT_NEAR( 10.0f, pRoot->getWorldPosition()._x, 1.0e-4f );
    SW_EXPECT_FALSE( pB->activate( pRider ) ); // 아직 안이다 — 몸의 다른 바디가 낸 진입 · 불러온 뒤의 진입도 되돌려 보내지 않는다
    OverlapInfo left;
    left._pOther = pRider;
    GameObjectEvents::broadcast( *pB->getOwner(), &GameObjectEvents::_overlapEnd, left );
    SW_EXPECT_TRUE( pB->activate( pRider ) ); // 나갔다 다시 들어왔다
    SW_EXPECT_NEAR( 0.0f, pRoot->getWorldPosition()._x, 1.0e-4f );
}

/** @brief [TeleportTriggerTest] 짝 포털이 꺼져 있는 동안 도착한 것은 기억하지 않는다 — 다시 켠 뒤 걸어 들어온 것을 삼키지 않는다 */
SW_TEST_CASE( TeleportTriggerTest, DisabledPairDoesNotSwallowTheNextEntry )
{
    using Internal = TestTeleportTriggerInternal;
    GameObjectManager         manager;
    TeleportTriggerComponent* pA = Internal::addPortal( manager, "PortalA", float3{ 0.0f, 0.0f, 0.0f }, 0.0f );
    TeleportTriggerComponent* pB = Internal::addPortal( manager, "PortalB", float3{ 10.0f, 2.0f, 0.0f }, 0.0f );
    pA->setDestination( pB->getOwner()->getHandle() );
    pB->setDestination( pA->getOwner()->getHandle() );
    GameObject* pRider = manager.createGameObject( hashed_string( "Rider" ) );
    (void)pRider->addComponent<SceneComponent>();
    pB->setActive( false );
    SW_EXPECT_TRUE( pA->activate( pRider ) );
    pB->setActive( true );
    SW_EXPECT_TRUE( pB->activate( pRider ) );
}

/** @brief [TeleportTriggerTest] 짝 포털의 볼륨 밖(거리 판정)에 도착하면 기다림이 남지 않는다 — 나중에 걸어 들어가면 옮긴다 */
SW_TEST_CASE( TeleportTriggerTest, ArrivalOutsideThePairIsForgotten )
{
    using Internal = TestTeleportTriggerInternal;
    GameObjectManager         manager;
    TeleportTriggerComponent* pA = Internal::addPortal( manager, "PortalA", float3{ 0.0f, 0.0f, 0.0f }, 0.0f );
    TeleportTriggerComponent* pB = Internal::addPortal( manager, "PortalB", float3{ 10.0f, 2.0f, 0.0f }, 0.0f );
    pA->setDestination( pB->getOwner()->getHandle() );
    pB->setDestination( pA->getOwner()->getHandle() );
    GameObject*     pRider = manager.createGameObject( hashed_string( "Rider" ) );
    SceneComponent* pRoot  = pRider->addComponent<SceneComponent>();
    pB->setProximity( pRider->getHandle(), 1.0f );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pA->activate( pRider ) );
    pRoot->setLocalPosition( float3{ 20.0f, 2.0f, 0.0f } ); // 잰 순간 볼륨 밖이다
    for ( int32 frame = 0; frame < 3; ++frame )
    {
        manager.update( 1.0f / 60.0f );
    }
    pRoot->setLocalPosition( float3{ 10.5f, 2.0f, 0.0f } );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_NEAR( 0.0f, pRoot->getWorldPosition()._x, 1.0e-4f ); // B 가 A 로 옮겼다
}

/** @brief [TeleportTriggerTest] 도착 오브젝트가 없으면 옮기지 않고 발동으로 치지 않는다(경고) */
SW_TEST_CASE( TeleportTriggerTest, MissingDestinationDoesNotFire )
{
    SW_EXPECT_LOG( "TeleportTrigger.CannotMove" );
    GameObjectManager         manager;
    TeleportTriggerComponent* pPortal = TestTeleportTriggerInternal::addPortal( manager, "Portal", float3{}, 0.0f );
    GameObject*               pRider  = manager.createGameObject( hashed_string( "Rider" ) );
    (void)pRider->addComponent<SceneComponent>();
    SW_EXPECT_FALSE( pPortal->activate( pRider ) );
    SW_EXPECT_FALSE( pPortal->hasFired() );
}
