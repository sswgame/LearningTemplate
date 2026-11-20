#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

// SequenceTriggerProgressTest — 지역 진입 컷씬의 진행 플래그(판 상태 플래그 — 세이브 · 같은 플래그의 볼륨끼리)와 이미 안에 나타난 활성자의 규칙(_spawnInside)을 본다.
// 판 상태(게임플레이 층)를 열어야 해서 시퀀스 트리거(캐릭터 층) 테스트와 나눠 이 층에 둔다(nogpu, 에셋 없음).

using namespace sw;

namespace
{
    /** @brief 열린 판 상태 · 시퀀스 플레이어 · 거리로 재는 볼륨 · 기수를 둔 씬입니다. 기수는 처음에 @p riderX 에 섭니다. */
    struct SequenceTriggerRig
    {
        GameObjectManager         _manager;
        SessionStateComponent*    _pSession{ nullptr };
        SequencePlayerComponent*  _pPlayer{ nullptr };
        SequenceTriggerComponent* _pTrigger{ nullptr };
        GameObject*               _pRider{ nullptr };
        SceneComponent*           _pRiderRoot{ nullptr };

        explicit SequenceTriggerRig( float32 riderX )
        {
            GameObject* pSessionObject = _manager.createGameObject( hashed_string( "Ranch" ) );
            SW_REQUIRE_NOT_NULL( pSessionObject );
            _pSession = pSessionObject->addComponent<SessionStateComponent>();
            SW_REQUIRE_NOT_NULL( _pSession );
            SW_REQUIRE_TRUE( _pSession->initialize( SessionStateSettings{} ) == SessionStateInitResult::Fresh );

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
    SW_EXPECT_LOG( "ActivatorVolume.ProgressFlagNoSession" );
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
    rig._pTrigger->setSpawnInside( ActivatorSpawnInside::Ignore );
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
    rig._pTrigger->setSpawnInside( ActivatorSpawnInside::Ignore );
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
