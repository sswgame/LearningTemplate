#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

// SequenceTriggerProgressTest — 지역 진입 컷씬의 진행 플래그(판 상태 플래그 — 세이브 · 같은 플래그의 볼륨끼리)와 이미 안에 나타난 활성자의 규칙(_spawnInside)을 본다.
// 판 상태(게임플레이 층)를 열어야 해서 시퀀스 트리거(캐릭터 층) 테스트와 나눠 이 층에 둔다(nogpu, 에셋 없음).

using namespace sw;

namespace
{
    /** @brief 열린 판 상태 · 시퀀스 플레이어 · 거리로 재는 볼륨 · 기수를 둔 씬입니다. 기수는 처음에 @p riderX 에 섭니다. */
    struct SequenceTriggerRig
    {
        GameObjectManager         _manager;
        GameStateComponent*       _pSession{ nullptr };
        SequencePlayerComponent*  _pPlayer{ nullptr };
        SequenceTriggerComponent* _pTrigger{ nullptr };
        GameObject*               _pRider{ nullptr };
        SceneComponent*           _pRiderRoot{ nullptr };

        explicit SequenceTriggerRig( float32 riderX )
        {
            GameObject* pSessionObject = _manager.createGameObject( hashed_string( "Ranch" ) );
            SW_REQUIRE_NOT_NULL( pSessionObject );
            _pSession = pSessionObject->addComponent<GameStateComponent>();
            SW_REQUIRE_NOT_NULL( _pSession );
            SW_REQUIRE_TRUE( _pSession->initialize( GameStateSettings{} ) == GameStateInitResult::Fresh );

            GameObject* pDirector = _manager.createGameObject( hashed_string( "Director" ) );
            SW_REQUIRE_NOT_NULL( pDirector );
            _pPlayer = pDirector->addComponent<SequencePlayerComponent>();
            SW_REQUIRE_NOT_NULL( _pPlayer );
            _pPlayer->setSequence( SequenceAsset{} );
            _pPlayer->setAutoPlay( false ); // 트리거가 트는 디렉터다 — 켜 두면 beginPlay 가 먼저 틀어 트리거가 "재생 중" 으로 본다

            _pRider = _manager.createGameObject( hashed_string( "Rider" ) );
            SW_REQUIRE_NOT_NULL( _pRider );
            _pRiderRoot = _pRider->addComponent<SceneComponent>();
            SW_REQUIRE_NOT_NULL( _pRiderRoot );
            _pRiderRoot->setWorldPosition( float3{ riderX, 0.0f, 0.0f } );

            GameObject* pVolumeObject = _manager.createGameObject( hashed_string( "TownArrival" ) );
            SW_REQUIRE_NOT_NULL( pVolumeObject );
            (void)pVolumeObject->addComponent<SceneComponent>();
            _pTrigger = pVolumeObject->addComponent<SequenceTriggerComponent>();
            SW_REQUIRE_NOT_NULL( _pTrigger );
            _pTrigger->setSequence( pDirector->getHandle() );
            _pTrigger->setProximity( _pRider->getHandle(), 2.0f );
            _pTrigger->setProgressFlag( hashed_string( "town.arrivalSeen" ) );
        }

        void tickFrames( uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( 1.0f / 60.0f );
            }
        }

        void moveRiderAndTick( float32 x )
        {
            _pRiderRoot->setWorldPosition( float3{ x, 0.0f, 0.0f } );
            tickFrames( 1 );
        }

        int32 getSeenFlag() const { return _pSession->getFlags().getFlag( hashed_string( "town.arrivalSeen" ) ); }
    };
} // namespace

/** @brief [SequenceTriggerProgressTest] 진행 플래그를 적은 볼륨은 판 상태에 본 기록을 세우고, 같은 플래그의 다른 볼륨 · 세이브에서 돌아온 플래그가 다시 틀지 않게 한다 */
SW_TEST_CASE( SequenceTriggerProgressTest, ProgressFlagIsTheOnceRecord )
{
    SequenceTriggerRig rig( 100.0f );
    SW_EXPECT_FALSE( rig._pTrigger->hasFired() );
    SW_EXPECT_TRUE( rig._pTrigger->activate( rig._pRider ) );
    SW_EXPECT_EQUAL( 1, rig.getSeenFlag() );
    SW_EXPECT_TRUE( rig._pTrigger->hasFired() );
    rig._pPlayer->stop();

    // 같은 플래그의 둘째 입구 — 판 상태가 이미 봤다고 말하므로 틀지 않는다
    GameObject* pSecondObject = rig._manager.createGameObject( hashed_string( "TownArrivalEast" ) );
    SW_REQUIRE_NOT_NULL( pSecondObject );
    SequenceTriggerComponent* pSecond = pSecondObject->addComponent<SequenceTriggerComponent>();
    SW_REQUIRE_NOT_NULL( pSecond );
    pSecond->setSequence( rig._pPlayer->getOwner()->getHandle() );
    pSecond->setProgressFlag( hashed_string( "town.arrivalSeen" ) );
    SW_EXPECT_TRUE( pSecond->hasFired() );
    SW_EXPECT_FALSE( pSecond->activate( rig._pRider ) );
    SW_EXPECT_FALSE( rig._pPlayer->isPlaying() );

    // 세이브 왕복 — 플래그 목록을 지웠다 되살리면(발동 수가 없는 새 볼륨이 판단한다) 여전히 틀지 않는다
    vector<GameFlagEntry> listEntry;
    rig._pSession->getFlags().fillEntries( listEntry );
    rig._pSession->getFlags().clear();
    SW_EXPECT_FALSE( pSecond->hasFired() );
    rig._pSession->getFlags().restoreEntries( listEntry );
    SW_EXPECT_TRUE( pSecond->hasFired() );
    SW_EXPECT_FALSE( pSecond->activate( rig._pRider ) );
    SW_EXPECT_EQUAL( 1, rig.getSeenFlag() );
}

/** @brief [SequenceTriggerProgressTest] 진행 플래그를 적었는데 열린 판 상태가 없으면 오류를 남기고 틀지 않는다(발동 수로 대신하지 않는다) */
SW_TEST_CASE( SequenceTriggerProgressTest, ProgressFlagWithoutSessionDoesNothing )
{
    SW_EXPECT_LOG( "Trigger.ProgressFlagNoSession" );
    GameObjectManager manager;
    GameObject*       pDirector = manager.createGameObject( hashed_string( "Director" ) );
    SW_REQUIRE_NOT_NULL( pDirector );
    SequencePlayerComponent* pPlayer = pDirector->addComponent<SequencePlayerComponent>();
    SW_REQUIRE_NOT_NULL( pPlayer );
    pPlayer->setSequence( SequenceAsset{} );

    GameObject* pVolumeObject = manager.createGameObject( hashed_string( "Gate" ) );
    SW_REQUIRE_NOT_NULL( pVolumeObject );
    SequenceTriggerComponent* pTrigger = pVolumeObject->addComponent<SequenceTriggerComponent>();
    SW_REQUIRE_NOT_NULL( pTrigger );
    pTrigger->setSequence( pDirector->getHandle() );
    pTrigger->setProgressFlag( hashed_string( "gate.seen" ) );

    GameObject* pRider = manager.createGameObject( hashed_string( "Rider" ) );
    SW_REQUIRE_NOT_NULL( pRider );
    SW_EXPECT_FALSE( pTrigger->activate( pRider ) );
    SW_EXPECT_FALSE( pPlayer->isPlaying() );
    SW_EXPECT_FALSE( pTrigger->hasFired() );
}

/** @brief [SequenceTriggerProgressTest] 기본(Activate)은 트리거 계약대로 안에서 나타난 활성자를 들어온 것으로 센다 */
SW_TEST_CASE( SequenceTriggerProgressTest, SpawnInsideActivatePlaysOnTheFirstLook )
{
    SequenceTriggerRig rig( 1.0f ); // 볼륨이 생길 때 이미 안이다
    rig._manager.beginPlay();
    rig.tickFrames( 1 );
    SW_EXPECT_TRUE( rig._pTrigger->hasFired() );
    SW_EXPECT_EQUAL( 1u, rig._pPlayer->getHistory().countKind( SequenceHistoryKind::Started ) );
}

/** @brief [SequenceTriggerProgressTest] Ignore 는 볼륨이 나타났을 때 이미 안에 있던 활성자에 틀지 않고, 나갔다 다시 들어올 때 튼다 */
SW_TEST_CASE( SequenceTriggerProgressTest, SpawnInsideIgnoreWaitsForTheWalkIn )
{
    SequenceTriggerRig rig( 1.0f );
    rig._pTrigger->setSpawnInside( TriggerSpawnInside::Ignore );
    rig._manager.beginPlay();
    rig.tickFrames( 20 ); // 볼륨이 나타난 창(8 프레임)을 지나도록 안에 머문다
    SW_EXPECT_FALSE( rig._pTrigger->hasFired() );
    SW_EXPECT_EQUAL( 0, rig.getSeenFlag() );

    rig.moveRiderAndTick( 50.0f );
    rig.moveRiderAndTick( 1.0f );
    SW_EXPECT_TRUE( rig._pTrigger->hasFired() );
    SW_EXPECT_EQUAL( 1, rig.getSeenFlag() );
    SW_EXPECT_EQUAL( 1u, rig._pPlayer->getHistory().countKind( SequenceHistoryKind::Started ) );
}

/** @brief [SequenceTriggerProgressTest] Ignore 는 볼륨보다 늦게 안에서 생긴 활성자(스폰 · 로그인 · 부활)에도 틀지 않는다 */
SW_TEST_CASE( SequenceTriggerProgressTest, SpawnInsideIgnoreSkipsAnActivatorBornInside )
{
    SequenceTriggerRig rig( 100.0f );
    rig._pTrigger->setSpawnInside( TriggerSpawnInside::Ignore );
    rig._manager.beginPlay();
    rig.tickFrames( 20 ); // 볼륨의 나타남 창은 지났다

    GameObject* pNewRider = rig._manager.createGameObject( hashed_string( "RespawnedRider" ) );
    SW_REQUIRE_NOT_NULL( pNewRider );
    SceneComponent* pNewRoot = pNewRider->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pNewRoot );
    pNewRoot->setWorldPosition( float3{ 1.0f, 0.0f, 0.0f } );
    rig._pTrigger->setProximity( pNewRider->getHandle(), 2.0f );
    rig.tickFrames( 3 );
    SW_EXPECT_FALSE( rig._pTrigger->hasFired() );

    pNewRoot->setWorldPosition( float3{ 50.0f, 0.0f, 0.0f } );
    rig.tickFrames( 20 ); // 나타남 창 둘(16 프레임)이 지나 더는 막 생긴 오브젝트가 아니다
    pNewRoot->setWorldPosition( float3{ 1.0f, 0.0f, 0.0f } );
    rig.tickFrames( 1 );
    SW_EXPECT_TRUE( rig._pTrigger->hasFired() );
}

/**
 * @brief [SequenceTriggerProgressTest] Ignore 는 트리거 콜라이더 안에서 생긴 몸(스폰 · 부활)에 틀지 않는다 — 첫 물리 스텝까지 스텝 없는 프레임이 수십 끼어도(높은 프레임률)
 * @details 겹침 길은 프레임 수 창이 아니라 물리의 "안에서 생김" 표시(`OverlapInfo::_bOtherSpawnedInside`)를 본다. 변이: `isOverlapSpawnEntry` 가 표시를 보지 않으면 첫 단언이 진다(예전 8 프레임 창도 졌다).
 */
SW_TEST_CASE( SequenceTriggerProgressTest, SpawnInsideIgnoreSkipsABodyBornInsideTheCollider )
{
    constexpr float32 kFrame = 1.0f / 60.0f;
    GameObjectManager manager;
    GameObject*       pDirector = manager.createGameObject( hashed_string( "Director" ) );
    SW_REQUIRE_NOT_NULL( pDirector );
    SequencePlayerComponent* pPlayer = pDirector->addComponent<SequencePlayerComponent>();
    SW_REQUIRE_NOT_NULL( pPlayer );
    pPlayer->setSequence( SequenceAsset{} );
    pPlayer->setAutoPlay( false );

    GameObject* pVolumeObject = manager.createGameObject( hashed_string( "TownArrival" ) );
    SW_REQUIRE_NOT_NULL( pVolumeObject );
    RigidBodyComponent* pTriggerBody = pVolumeObject->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pTriggerBody );
    PhysicsShapeDesc3D triggerBox;
    triggerBox._type        = PhysicsShapeType3D::Box;
    triggerBox._halfExtents = float3{ 2.0f, 2.0f, 2.0f };
    pTriggerBody->setShape( triggerBox );
    pTriggerBody->setBodyType( PhysicsBodyType::Static );
    pTriggerBody->setTrigger( true );
    pTriggerBody->setLayer( hashed_string( "Trigger" ) );
    SequenceTriggerComponent* pTrigger = pVolumeObject->addComponent<SequenceTriggerComponent>();
    SW_REQUIRE_NOT_NULL( pTrigger );
    pTrigger->setSequence( pDirector->getHandle() );
    pTrigger->setSpawnInside( TriggerSpawnInside::Ignore );
    manager.beginPlay();
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        manager.update( kFrame );
    }

    GameObject* pRider = manager.createGameObject( hashed_string( "RespawnedRider" ) );
    SW_REQUIRE_NOT_NULL( pRider );
    RigidBodyComponent* pRiderBody = pRider->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pRiderBody );
    PhysicsShapeDesc3D riderBox;
    riderBox._type        = PhysicsShapeType3D::Box;
    riderBox._halfExtents = float3{ 0.25f, 0.25f, 0.25f };
    pRiderBody->setShape( riderBox );
    pRiderBody->setBodyType( PhysicsBodyType::Dynamic );
    pRiderBody->setGravityFactor( 0.0f );
    const ScenePhysics& physics    = manager.getSubsystem<ScenePhysics>();
    const uint64        stepStart  = physics.getStepCount();
    uint32              frameCount = 0;
    while ( physics.getStepCount() == stepStart && frameCount < 1000 )
    {
        manager.update( kFrame / 64.0f ); // 고정 스텝보다 훨씬 짧은 프레임
        ++frameCount;
    }
    SW_REQUIRE_TRUE( frameCount > static_cast<uint32>( TriggerSpawnWindow::kFrames ) );
    manager.update( kFrame );
    SW_REQUIRE_EQUAL( 1u, physics.getOverlapTracker().getOverlapCount( pRider->getObjectID() ) ); // 진입은 왔다
    SW_EXPECT_FALSE( pTrigger->hasFired() );

    pRiderBody->setLocalPosition( float3{ 10.0f, 0.0f, 0.0f } );
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        manager.update( kFrame );
    }
    pRiderBody->setLocalPosition( float3{ 0.0f, 0.0f, 0.0f } );
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        manager.update( kFrame );
    }
    SW_EXPECT_TRUE( pTrigger->hasFired() );
}
