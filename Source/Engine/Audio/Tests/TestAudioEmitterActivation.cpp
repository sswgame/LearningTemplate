// AudioEmitterActivationTest — 에미터의 소리가 주인의 계층 활성을 따른다: 끄면 멈추고, 다시 켜면 Play On Begin Play 로 다시 낸다(유니티 AudioSource 의미). 오프라인 엔진(장치 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/Component/AudioEmitterComponent.h"
#include "Engine/Audio/Scene/SceneAudio.h"
#include "Engine/Audio/Tests/OfflineAudioEngine.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/**
 * @brief [AudioEmitterActivationTest] 반복 소리를 내는 에미터의 주인을 끄면 인스턴스가 사라지고, 다시 켜면 다시 재생된다 — 풀에 든 적의 소리가 마지막 자리에서 계속 나던 결함(K-11)
 * @details 변이: `onOwnerActiveInHierarchyChanged` 의 멈춤을 빼면 둘째 단언이, 다시 내기를 빼면 셋째 단언이 진다. 켠 채로 둔 대조 구간에서 인스턴스가 그대로인지도 본다.
 */
SW_TEST_CASE( AudioEmitterActivationTest, OwnerDeactivationStopsAndReactivationReplays )
{
    test::OfflineAudioEngine offline;
    GameObjectManager        manager;
    manager.getSubsystem<SceneAudio>().setAudioEngine( &offline.getEngine() );
    GameObject* pObject = manager.createGameObject( hashed_string( "PooledEnemy" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    AudioEmitterComponent* pEmitter = pObject->addComponent<AudioEmitterComponent>();
    SW_REQUIRE_NOT_NULL( pEmitter );
    pEmitter->setEvent( hashed_string( test::OfflineAudioEngine::kLoopEvent ) );
    pEmitter->setPlayOnBeginPlay( true );
    manager.mergePendingAdds();
    manager.beginPlay();

    offline.renderBlocks( 4 );
    SW_REQUIRE_EQUAL( 1u, offline.getInstanceCount() ); // 시작할 때 냈다
    offline.renderBlocks( 4 );
    SW_EXPECT_EQUAL( 1u, offline.getInstanceCount() ); // 대조: 켜진 채로는 반복 소리가 그대로 산다

    pObject->setActive( false );
    offline.renderBlocks( 4 );
    SW_EXPECT_EQUAL( 0u, offline.getInstanceCount() ); // 끄면 멈춘다

    pObject->setActive( true );
    offline.renderBlocks( 4 );
    SW_EXPECT_EQUAL( 1u, offline.getInstanceCount() ); // 다시 켜면 다시 낸다

    manager.getSubsystem<SceneAudio>().setAudioEngine( nullptr );
}
