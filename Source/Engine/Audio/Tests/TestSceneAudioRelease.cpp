/**
 * @file TestSceneAudioRelease.cpp
 * @brief 씬 오디오가 프로세스에 하나인 오디오 엔진에 걸어 둔 것이 씬과 함께 내려가는가 · 월드 원점 이동이 엔진이 든 자리에 닿는가입니다(오프라인 엔진).
 */
#include "pch.h"

#include "Core/Math/Math.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Audio/AudioSnapshotMixer.h"
#include "Engine/Audio/Component/AudioListenerComponent.h"
#include "Engine/Audio/Component/AudioReverbZoneComponent.h"
#include "Engine/Audio/Scene/SceneAudio.h"
#include "Engine/Audio/Tests/OfflineAudioEngine.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`). */
    struct AudioEngineTestAccess
    {
        static float3 getEmitterPosition( const AudioEngine& audioEngine, AudioEmitterID emitterID )
        {
            const auto it = audioEngine._mapEmitter.find( emitterID );
            return it != audioEngine._mapEmitter.end() ? it->second._state._position : float3{};
        }
        static float3 getListenerPosition( const AudioEngine& audioEngine, uint32 listenerIndex ) { return audioEngine._arrListener[listenerIndex]._position; }
    };
} // namespace sw

using namespace sw;

/**
 * @brief [SceneAudioReleaseTest] 리버브 존 안에서 씬이 비면 그 스냅샷의 세기가 0 으로 내려간다 — 다음 씬에 동굴 리버브가 남지 않는다
 * @details 변이: `SceneAudio::onSceneCleared` 가 걸어 둔 스냅샷을 내리지 않으면 마지막 단언이 진다.
 */
SW_TEST_CASE( SceneAudioReleaseTest, ReverbSnapshotLeavesWithTheScene )
{
    test::OfflineAudioEngine offline;
    const hashed_string      snapshot{ "TestCave" };
    AudioMixerDef            mixer = AudioMixerDef::makeDefault();
    AudioSnapshotDef         cave{};
    cave._name = snapshot;
    mixer._listSnapshot.push_back( cave );
    SW_REQUIRE_TRUE( offline.getEngine().loadMixer( mixer ) );
    offline.renderBlocks( 1 );

    GameObjectManager manager;
    SceneAudio&       sceneAudio = manager.getSubsystem<SceneAudio>();
    sceneAudio.setAudioEngine( &offline.getEngine() );
    GameObject* const pObject = manager.createGameObject( hashed_string( "Cave" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    AudioReverbZoneComponent* const pZone = pObject->addComponent<AudioReverbZoneComponent>();
    SW_REQUIRE_NOT_NULL( pZone );
    SW_REQUIRE_NOT_NULL( pObject->addComponent<AudioListenerComponent>() );
    pZone->setSnapshot( snapshot );
    manager.mergePendingAdds();
    manager.beginPlay();

    sceneAudio.update( 0.016f, nullptr, nullptr );
    offline.renderBlocks( 1 );
    SW_REQUIRE_TRUE( offline.getEngine().getSnapshotMixer().getSnapshotIntensity( snapshot ) > 0.0f );

    sceneAudio.onSceneCleared();
    offline.renderBlocks( 1 );
    SW_EXPECT_EQUAL( 0.0f, offline.getEngine().getSnapshotMixer().getSnapshotIntensity( snapshot ) );
    sceneAudio.setAudioEngine( nullptr );
}

/**
 * @brief [SceneAudioReleaseTest] 월드 원점이 옮겨지면 엔진이 든 자리(리스너 · 씬이 다시 넣지 않는 한 번짜리 에미터)도 같은 만큼 옮겨진다
 * @details 변이: `onWorldOriginShifted` 가 엔진에 알리지 않으면 둘 다 옛 자리라 진다.
 */
SW_TEST_CASE( SceneAudioReleaseTest, OriginShiftMovesPositionsHeldByTheEngine )
{
    test::OfflineAudioEngine offline;
    GameObjectManager        manager;
    SceneAudio&              sceneAudio = manager.getSubsystem<SceneAudio>();
    sceneAudio.setAudioEngine( &offline.getEngine() );
    constexpr AudioEmitterID kEmitter = 7;
    offline.getEngine().setEmitter( kEmitter, float3{ 1.0f, 2.0f, 3.0f }, float3{} );
    AudioListenerState listener{};
    listener._bActive = SW_TRUE;
    offline.getEngine().setListener( 0, listener );

    sceneAudio.onWorldOriginShifted( float3{ -1000.0f, 0.0f, 500.0f } );
    offline.renderBlocks( 1 );
    SW_EXPECT_TRUE( ( AudioEngineTestAccess::getEmitterPosition( offline.getEngine(), kEmitter ) == float3{ -999.0f, 2.0f, 503.0f } ) );
    SW_EXPECT_TRUE( ( AudioEngineTestAccess::getListenerPosition( offline.getEngine(), 0 ) == float3{ -1000.0f, 0.0f, 500.0f } ) );
    sceneAudio.setAudioEngine( nullptr );
}
