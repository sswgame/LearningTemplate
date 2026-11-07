#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"

// SequenceTriggerTest — 들어오면 시퀀스 플레이어를 틀고(한 번), 플레이어가 없는 오브젝트를 가리키면 발동한 것으로 치지 않는다(nogpu, 에셋 없음).

using namespace sw;

/** @brief [SequenceTriggerTest] 활성자가 들어오면 가리킨 오브젝트의 시퀀스를 틀고, 한 번만 하는 볼륨은 다시 틀지 않는다 */
SW_TEST_CASE( SequenceTriggerTest, ActivationPlaysTheSequenceOnce )
{
    GameObjectManager manager;
    GameObject*       pDirector = manager.createGameObject( hashed_string( "Director" ) );
    SW_REQUIRE_NOT_NULL( pDirector );
    SequencePlayerComponent* pPlayer = pDirector->addComponent<SequencePlayerComponent>();
    SW_REQUIRE_NOT_NULL( pPlayer );
    pPlayer->setSequence( SequenceAsset{} );

    GameObject* pVolumeObject = manager.createGameObject( hashed_string( "Gate" ) );
    SW_REQUIRE_NOT_NULL( pVolumeObject );
    (void)pVolumeObject->addComponent<SceneComponent>();
    SequenceTriggerComponent* pTrigger = pVolumeObject->addComponent<SequenceTriggerComponent>();
    SW_REQUIRE_NOT_NULL( pTrigger );
    pTrigger->setSequence( pDirector->getHandle() );

    GameObject* pRider = manager.createGameObject( hashed_string( "Rider" ) );
    SW_REQUIRE_NOT_NULL( pRider );

    SW_EXPECT_FALSE( pPlayer->isPlaying() );
    SW_EXPECT_TRUE( pTrigger->activate( pRider ) );
    SW_EXPECT_TRUE( pPlayer->isPlaying() );
    SW_EXPECT_TRUE( pTrigger->hasFired() );

    pPlayer->stop();
    SW_EXPECT_FALSE( pTrigger->activate( pRider ) );
    SW_EXPECT_FALSE( pPlayer->isPlaying() );
}

/** @brief [SequenceTriggerTest] 시퀀스 플레이어가 없는 오브젝트를 가리키면 틀지 못해 발동하지 않은 것으로 남는다 */
SW_TEST_CASE( SequenceTriggerTest, MissingPlayerDoesNotCountAsFired )
{
    SW_EXPECT_LOG( "SequenceTrigger.SequenceObjectNoPlayer" );
    GameObjectManager manager;
    GameObject*       pEmpty = manager.createGameObject( hashed_string( "Empty" ) );
    SW_REQUIRE_NOT_NULL( pEmpty );

    GameObject* pVolumeObject = manager.createGameObject( hashed_string( "Gate" ) );
    SW_REQUIRE_NOT_NULL( pVolumeObject );
    SequenceTriggerComponent* pTrigger = pVolumeObject->addComponent<SequenceTriggerComponent>();
    SW_REQUIRE_NOT_NULL( pTrigger );
    pTrigger->setSequence( pEmpty->getHandle() );

    GameObject* pRider = manager.createGameObject( hashed_string( "Rider" ) );
    SW_REQUIRE_NOT_NULL( pRider );
    SW_EXPECT_FALSE( pTrigger->activate( pRider ) );
    SW_EXPECT_FALSE( pTrigger->hasFired() );
}
