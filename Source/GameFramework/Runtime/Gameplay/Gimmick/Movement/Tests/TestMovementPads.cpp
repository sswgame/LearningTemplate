// MovementPadTest — 발사대가 겹친 동적 강체를 쏘는지(실제 2D 물리, Box2D), 부스트 패드가 부스트 계약(IBoostReceiver — 탈것 · 테스트 받는 쪽)에 부스트를 거는지 본다(nogpu). 받는 쪽이 하나라 이벤트 없이 직접 부른다.
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/BoostPadComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/LaunchPadComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/Tests/BoostReceiverTestComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/KinematicVehicleComponent.h"

using namespace sw;

namespace
{
    struct TestMovementPadsInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        static RigidBody2DComponent* addBox( GameObject& object, const float3& localPosition, float32 halfExtent, PhysicsBodyType type, bool bTrigger )
        {
            RigidBody2DComponent* pBody = object.addComponent<RigidBody2DComponent>();
            PhysicsShapeDesc2D    box;
            box._type        = PhysicsShapeType2D::Box;
            box._halfExtents = float2{ halfExtent, halfExtent };
            pBody->setShape( box );
            pBody->setBodyType( type );
            pBody->setGravityFactor( 0.0f );
            pBody->setTrigger( bTrigger );
            pBody->setLocalPosition( localPosition );
            return pBody;
        }

        static void run( GameObjectManager& manager, uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                manager.update( kFrame );
            }
        }
    };
} // namespace

/**
 * @brief [MovementPadTest] 발사대 트리거 안에 생긴 동적 강체는 발사 속도(기본 위로 12 m/s)를 그대로 받고, 한 번만 쏜다
 */
SW_TEST_CASE( MovementPadTest, LaunchPadSetsDynamicBodyVelocity )
{
    using Internal = TestMovementPadsInternal;
    GameObjectManager manager;
    GameObject*       pPad = manager.createGameObject( hashed_string( "Pad" ) );
    (void)Internal::addBox( *pPad, float3{}, 2.0f, PhysicsBodyType::Static, true );
    LaunchPadComponent* pLaunch = pPad->addComponent<LaunchPadComponent>();
    SW_REQUIRE_NOT_NULL( pLaunch );
    manager.beginPlay();
    Internal::run( manager, 2 );

    GameObject*           pCrate = manager.createGameObject( hashed_string( "Crate" ) );
    RigidBody2DComponent* pBody  = Internal::addBox( *pCrate, float3{}, 0.25f, PhysicsBodyType::Dynamic, false );
    SW_REQUIRE_NOT_NULL( pBody );
    Internal::run( manager, 4 );

    SW_EXPECT_EQUAL( 1, pLaunch->getLaunchCount() );
    SW_EXPECT_NEAR( 12.0f, pBody->getLinearVelocity()._y, 0.5f ); // 중력 0 — 쏜 속도가 남는다
    SW_EXPECT_NEAR( 0.0f, pBody->getLinearVelocity()._x, 1e-3f );
}

/**
 * @brief [MovementPadTest] 부스트 패드는 탈것 모터(계약을 포트로 내보냄)에 패드 시간(기본 1 초)만큼 부스트를 걸고, 계약이 없는 몸은 지나간다
 * @details 플레이를 시작하지 않는다 — 탈것 시작은 튜닝 에셋을 읽는데, 이 테스트는 패드 → 모터 길만 본다(기본 모터 설정은 부스트 모듈이 켜져 있다).
 */
SW_TEST_CASE( MovementPadTest, BoostPadStartsVehicleBoost )
{
    GameObjectManager          manager;
    BoostPadComponent*         pPad     = manager.createGameObject( hashed_string( "Pad" ) )->addComponent<BoostPadComponent>();
    GameObject*                pKart    = manager.createGameObject( hashed_string( "Kart" ) );
    KinematicVehicleComponent* pVehicle = pKart->addComponent<KinematicVehicleComponent>();
    GameObject*                pWalker  = manager.createGameObject( hashed_string( "Walker" ) );
    SW_REQUIRE_NOT_NULL( pPad );
    SW_REQUIRE_NOT_NULL( pVehicle );
    SW_REQUIRE_FALSE( pVehicle->getMotor().isBoosting() );

    pPad->boost( *pWalker );
    SW_EXPECT_EQUAL( 0, pPad->getBoostCount() );

    pPad->boost( *pKart );
    SW_EXPECT_EQUAL( 1, pPad->getBoostCount() );
    SW_EXPECT_TRUE( pVehicle->getMotor().isBoosting() );
    SW_EXPECT_NEAR( 1.0f, pVehicle->getMotor().getBoostTime(), 1e-5f );
}

/**
 * @brief [MovementPadTest] 부스트 패드는 계약(`IBoostReceiver`)만 본다 — 테스트 받는 쪽도 패드 시간을 받고, 거절하면 세지 않고 경고를 남긴다
 */
SW_TEST_CASE( MovementPadTest, BoostPadUsesTheBoostContract )
{
    SW_EXPECT_LOG( "BoostPadComponent.BoostRefused" );
    GameObjectManager           manager;
    BoostPadComponent*          pPad      = manager.createGameObject( hashed_string( "Pad" ) )->addComponent<BoostPadComponent>();
    GameObject*                 pSled     = manager.createGameObject( hashed_string( "Sled" ) );
    BoostReceiverTestComponent* pReceiver = pSled->addComponent<BoostReceiverTestComponent>();
    SW_REQUIRE_NOT_NULL( pPad );
    SW_REQUIRE_NOT_NULL( pReceiver );

    pPad->boost( *pSled );
    SW_EXPECT_EQUAL( 1, pPad->getBoostCount() );
    SW_EXPECT_NEAR( 1.0f, pReceiver->_lastDuration, 1e-5f );

    pReceiver->_bAccept = SW_FALSE;
    pPad->boost( *pSled );
    SW_EXPECT_EQUAL( 1, pPad->getBoostCount() );
}
