#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Audio/AudioAmbientEmitterComponent.h"
#include "Engine/Object/Component/Audio/AudioEmitterComponent.h"
#include "Engine/Object/Component/Audio/AudioListenerComponent.h"
#include "Engine/Object/Component/Audio/AudioReverbZoneComponent.h"
#include "Engine/Object/Component/Audio/PhysicsAudioOcclusionQuery.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneAudio.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioSceneTest — 씬 묶기: 게임 카메라 리스너(직교면 2D) · 리스너 컴포넌트 · 에미터 컴포넌트 · 리버브 존 → 스냅샷 · 앰비언트 가장 가까운 점 · 물리 레이캐스트 가림
// ------------------------------------------------------------------------------

namespace
{
    struct AudioSceneTestInternal
    {
        static constexpr const utf8* kMixerXML = R"(
<AudioMixerDesc>
	<_listBus>
		<AudioBusDesc _name="master" />
		<AudioBusDesc _name="sfx" _parent="master" />
	</_listBus>
	<_listAttenuation>
		<AudioAttenuationDesc _name="Flat" _curve="Linear" _minDistance="100" _maxDistance="200" />
		<AudioAttenuationDesc _name="Near" _curve="Linear" _minDistance="1" _maxDistance="10" />
	</_listAttenuation>
	<_occlusion _volumeDb="-12" _lowPassHz="900" _smoothingSeconds="0" />
	<_listSnapshot>
		<AudioSnapshotDesc _name="Cave"><_listBusVolume><AudioSnapshotBusDesc _bus="sfx" _volumeDb="-12" /></_listBusVolume></AudioSnapshotDesc>
	</_listSnapshot>
</AudioMixerDesc>)";

        static constexpr const utf8* kLibraryXML = R"(
<AudioEventLibrary>
	<_listEvent>
		<AudioEventDesc _name="Probe" _bus="sfx" _attenuation="Flat" _bLoop="true"><_listClip><AudioClipEntry _path="test/one" /></_listClip></AudioEventDesc>
		<AudioEventDesc _name="Shot" _bus="sfx" _attenuation="Near"><_listClip><AudioClipEntry _path="test/long" /></_listClip></AudioEventDesc>
	</_listEvent>
</AudioEventLibrary>)";

        /** @brief 이 시험의 그래프 · 라이브러리 · 직류 클립(스테레오 1.0, 길이 1 초 0.5)을 올린 엔진입니다. */
        static bool initializeEngine( sw::AudioEngine& engine )
        {
            sw::AudioMixerDesc mixerDesc;
            if ( mixerDesc.loadFromXMLText( kMixerXML ) == false || engine.initialize( mixerDesc ) == false )
                return false;
            engine.getClipStore().addClip( sw::hashed_string( "test/one" ), test::AudioTestUtil::makeConstantClip( 1.0f, 4800 ) );
            engine.getClipStore().addClip( sw::hashed_string( "test/long" ), test::AudioTestUtil::makeConstantClip( 0.5f, 48000 ) );
            sw::AudioEventLibrary library;
            return library.loadFromXMLText( kLibraryXML ) && engine.loadEventLibrary( sw::hashed_string( "test/scene" ), library );
        }

        /** @brief 세 블록을 렌더하고 마지막 블록의 채널 평균입니다. */
        static float32 renderMean( sw::AudioEngine& engine, uint32 channel )
        {
            (void)test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount * 2 );
            const sw::vector<float32> listSample = test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount );
            return test::AudioTestUtil::computeMean( listSample, channel, 0, sw::audio::kBlockFrameCount );
        }

        /** @brief 늘 같은 가림을 돌려주는 질의입니다. */
        struct ConstantOcclusion final : public sw::IAudioOcclusionQuery
        {
            float32 _value{ 1.0f };
            float32 computeOcclusion( const sw::float3&, const sw::float3& ) const override { return _value; }
        };
    };
} // namespace

/**
 * @brief [AudioSceneTest] 리스너 컴포넌트가 없으면 게임 카메라가 리스너다 — 원근이면 카메라 축으로, 직교면 화면 평면(2D, 반폭 = 직교 높이 × 16/9 ÷ 2)으로 팬한다
 */
SW_TEST_CASE( AudioSceneTest, CameraIsTheDefaultListenerAndOrthoIs2D )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioSceneTestInternal::initializeEngine( engine ) );
    sw::GameObjectManager manager;
    manager.getSceneAudio().setAudioEngine( &engine );

    sw::GameObject*            pCameraObject = manager.createGameObject( sw::hashed_string( "Camera" ) );
    sw::CameraComponent*       pCamera       = pCameraObject->addComponent<sw::CameraComponent>();
    sw::GameObject*            pSource       = manager.createGameObject( sw::hashed_string( "Source" ) );
    sw::AudioEmitterComponent* pEmitter      = pSource->addComponent<sw::AudioEmitterComponent>();
    SW_ASSERT_TRUE( pCamera != nullptr && pEmitter != nullptr );
    pEmitter->setWorldPosition( sw::float3( 4.0f, 0.0f, 0.0f ) );
    manager.flushSceneTransforms();

    manager.getSceneAudio().update( 1.0f / 60.0f, pCamera, nullptr );
    SW_ASSERT_TRUE( pEmitter->post( sw::hashed_string( "Probe" ) ) != 0 );
    // 원근 카메라(+Z 를 본다): 오른쪽 4 m 는 오른쪽 끝 — 스테레오 밸런스로 왼쪽 0.
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioSceneTestInternal::renderMean( engine, 0 ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, AudioSceneTestInternal::renderMean( engine, 1 ), 1e-3f );

    // 직교 높이 9 → 반폭 8: 가로 4 m 는 팬 0.5, 세로(위 5 m)는 팬에 들지 않는다.
    pCamera->setOrthographic( true );
    pCamera->setOrthoHeight( 9.0f );
    pEmitter->setWorldPosition( sw::float3( 4.0f, 5.0f, 0.0f ) );
    manager.flushSceneTransforms();
    manager.getSceneAudio().update( 1.0f / 60.0f, pCamera, nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.5f, AudioSceneTestInternal::renderMean( engine, 0 ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, AudioSceneTestInternal::renderMean( engine, 1 ), 1e-3f );

    // 리스너 컴포넌트가 생기면 카메라 대신 그것이다(오른쪽을 보는 리스너 — 같은 소리가 앞이 된다).
    sw::GameObject*             pEars     = manager.createGameObject( sw::hashed_string( "Ears" ) );
    sw::AudioListenerComponent* pListener = pEars->addComponent<sw::AudioListenerComponent>();
    pListener->setLocalRotation( sw::float3( 0.0f, sw::MathUtil::kHalfPi, 0.0f ) );
    pEmitter->setWorldPosition( sw::float3( 4.0f, 0.0f, 0.0f ) );
    manager.flushSceneTransforms();
    manager.getSceneAudio().update( 1.0f / 60.0f, pCamera, nullptr );
    SW_EXPECT_NEAR_EQUAL( AudioSceneTestInternal::renderMean( engine, 0 ), AudioSceneTestInternal::renderMean( engine, 1 ), 1e-3f );
}

/**
 * @brief [AudioSceneTest] 리버브 존 — 리스너가 들어간 깊이로 스냅샷 세기를 건다(경계 0 → 블렌드 거리 안쪽 1), 나가면 0
 */
SW_TEST_CASE( AudioSceneTest, ReverbZoneDrivesSnapshotIntensity )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioSceneTestInternal::initializeEngine( engine ) );
    sw::GameObjectManager manager;
    manager.getSceneAudio().setAudioEngine( &engine );
    sw::GameObject*               pZoneObject = manager.createGameObject( sw::hashed_string( "Cave" ) );
    sw::AudioReverbZoneComponent* pZone       = pZoneObject->addComponent<sw::AudioReverbZoneComponent>();
    pZone->setSnapshot( sw::hashed_string( "Cave" ) );
    pZone->setShape( sw::AudioVolumeShape::Box, sw::float3( 5.0f, 5.0f, 5.0f ), 0.0f, 2.0f );
    sw::GameObject*             pEars     = manager.createGameObject( sw::hashed_string( "Ears" ) );
    sw::AudioListenerComponent* pListener = pEars->addComponent<sw::AudioListenerComponent>();

    const float32 arrX[4]        = { 0.0f, 4.0f, 6.0f, 0.0f };
    const float32 arrExpected[4] = { 1.0f, 0.5f, 0.0f, 1.0f };
    for ( uint32 caseIndex = 0; caseIndex < 4; ++caseIndex )
    {
        pListener->setWorldPosition( sw::float3( arrX[caseIndex], 0.0f, 0.0f ) );
        manager.flushSceneTransforms();
        manager.getSceneAudio().update( 1.0f / 60.0f, nullptr, nullptr );
        (void)test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount );
        SW_EXPECT_NEAR_EQUAL( arrExpected[caseIndex], engine.getSnapshotIntensity( sw::hashed_string( "Cave" ) ), 1e-4f );
    }
    // 존이 사라지면 세기가 빠진다.
    manager.destroyObject( pZoneObject );
    manager.tick( 1.0f / 60.0f );
    manager.getSceneAudio().update( 1.0f / 60.0f, nullptr, nullptr );
    (void)test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount );
    SW_EXPECT_NEAR_EQUAL( 0.0f, engine.getSnapshotIntensity( sw::hashed_string( "Cave" ) ), 1e-4f );
}

/**
 * @brief [AudioSceneTest] 앰비언트 영역 에미터는 리스너 쪽 가장 가까운 점에서 소리를 낸다(안에 서면 리스너 자리)
 */
SW_TEST_CASE( AudioSceneTest, AmbientEmitterUsesTheNearestPoint )
{
    sw::GameObjectManager             manager;
    sw::GameObject*                   pRiver   = manager.createGameObject( sw::hashed_string( "River" ) );
    sw::AudioAmbientEmitterComponent* pAmbient = pRiver->addComponent<sw::AudioAmbientEmitterComponent>();
    pAmbient->setShape( sw::AudioVolumeShape::Box, sw::float3( 10.0f, 1.0f, 10.0f ), 0.0f );
    manager.flushSceneTransforms();
    SW_EXPECT_FALSE( pAmbient->usesOcclusion() );

    const sw::float3 outside = pAmbient->computeAudioPosition( sw::float3( 3.0f, 0.0f, 25.0f ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, outside._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, outside._z, 1e-5f );
    const sw::float3 inside = pAmbient->computeAudioPosition( sw::float3( -2.0f, 0.5f, 4.0f ) );
    SW_EXPECT_NEAR_EQUAL( -2.0f, inside._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, inside._z, 1e-5f );

    pAmbient->setShape( sw::AudioVolumeShape::Sphere, sw::float3( 0.0f, 0.0f, 0.0f ), 5.0f );
    const sw::float3 sphereEdge = pAmbient->computeAudioPosition( sw::float3( 0.0f, 0.0f, 20.0f ) );
    SW_EXPECT_NEAR_EQUAL( 5.0f, sphereEdge._z, 1e-5f );
}

/**
 * @brief [AudioSceneTest] 가림 — 물리 레이캐스트(레이 셋)가 벽 뒤는 1, 기둥 하나는 1/3, 트인 길은 0 이고, 씬 오디오가 그 값을 엔진에 넣어 소리가 -12 dB 가 된다
 */
SW_TEST_CASE( AudioSceneTest, PhysicsRaycastOcclusion )
{
    sw::PhysicsSettings settings;
    settings.ensureDefaults();
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = sw::engine::getPhysicsSystem().createScene3D( settings );
    SW_ASSERT_TRUE( pScene != nullptr );
    sw::PhysicsBodyDesc<sw::PhysicsDimension3D> wall;
    wall._type = sw::PhysicsBodyType::Static;
    sw::PhysicsShapeDesc3D wallShape;
    wallShape._type        = sw::PhysicsShapeType3D::Box;
    wallShape._halfExtents = sw::float3( 5.0f, 5.0f, 0.2f );
    wall._listShape.push_back( wallShape );
    wall._position = sw::float3( 0.0f, 0.0f, 5.0f );
    SW_ASSERT_TRUE( pScene->createBody( wall ).isValid() );
    sw::PhysicsBodyDesc<sw::PhysicsDimension3D> post = wall;
    post._listShape[0]._halfExtents                  = sw::float3( 0.1f, 5.0f, 0.1f );
    post._position                                   = sw::float3( 30.0f, 0.0f, 5.0f );
    SW_ASSERT_TRUE( pScene->createBody( post ).isValid() );
    pScene->step( 1.0f / 60.0f );

    sw::PhysicsAudioOcclusionQuery query;
    query.setScene( pScene.get(), sw::MathUtil::kMaxUInt32 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, query.computeOcclusion( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 0.0f, 0.0f, 10.0f ) ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, query.computeOcclusion( sw::float3( 20.0f, 0.0f, 0.0f ), sw::float3( 20.0f, 0.0f, 10.0f ) ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f / 3.0f, query.computeOcclusion( sw::float3( 30.0f, 0.0f, 0.0f ), sw::float3( 30.0f, 0.0f, 10.0f ) ), 1e-6f );

    // 씬 오디오가 가림을 엔진에 넣는다: 막힌 에미터는 -12 dB(직류는 로우패스를 그대로 지난다).
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioSceneTestInternal::initializeEngine( engine ) );
    sw::GameObjectManager manager;
    manager.getSceneAudio().setAudioEngine( &engine );
    sw::GameObject*             pEars     = manager.createGameObject( sw::hashed_string( "Ears" ) );
    sw::AudioListenerComponent* pListener = pEars->addComponent<sw::AudioListenerComponent>();
    sw::GameObject*             pSource   = manager.createGameObject( sw::hashed_string( "Source" ) );
    sw::AudioEmitterComponent*  pEmitter  = pSource->addComponent<sw::AudioEmitterComponent>();
    SW_ASSERT_TRUE( pListener != nullptr );
    pEmitter->setWorldPosition( sw::float3( 0.0f, 0.0f, 10.0f ) );
    manager.flushSceneTransforms();
    manager.getSceneAudio().update( 1.0f / 60.0f, nullptr, pScene.get() );
    SW_ASSERT_TRUE( pEmitter->post( sw::hashed_string( "Probe" ) ) != 0 );
    SW_EXPECT_NEAR_EQUAL( -12.0f, sw::AudioMath::linearToDb( AudioSceneTestInternal::renderMean( engine, 0 ) ), 0.05f );

    // 가림을 끄면(질의 0) 다시 0 dB.
    AudioSceneTestInternal::ConstantOcclusion open;
    open._value = 0.0f;
    manager.getSceneAudio().setOcclusionQuery( &open );
    manager.getSceneAudio().update( 1.0f / 60.0f, nullptr, pScene.get() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::AudioMath::linearToDb( AudioSceneTestInternal::renderMean( engine, 0 ) ), 0.05f );
    manager.getSceneAudio().setOcclusionQuery( nullptr );
}

/**
 * @brief [AudioSceneTest] 에미터 컴포넌트가 사라져도 그 자리의 원샷은 마지막 자리에서 끝까지 간다 — 2D(크게)로 돌아가지 않는다
 */
SW_TEST_CASE( AudioSceneTest, RemovedEmitterKeepsItsLastPlace )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioSceneTestInternal::initializeEngine( engine ) );
    sw::GameObjectManager manager;
    manager.getSceneAudio().setAudioEngine( &engine );
    sw::GameObject*             pEars     = manager.createGameObject( sw::hashed_string( "Ears" ) );
    sw::AudioListenerComponent* pListener = pEars->addComponent<sw::AudioListenerComponent>();
    sw::GameObject*             pSource   = manager.createGameObject( sw::hashed_string( "Bullet" ) );
    sw::AudioEmitterComponent*  pEmitter  = pSource->addComponent<sw::AudioEmitterComponent>();
    SW_ASSERT_TRUE( pListener != nullptr );
    pEmitter->setWorldPosition( sw::float3( 0.0f, 0.0f, 9.0f ) );
    manager.flushSceneTransforms();
    manager.getSceneAudio().update( 1.0f / 60.0f, nullptr, nullptr );
    SW_ASSERT_TRUE( pEmitter->post( sw::hashed_string( "Shot" ) ) != 0 );
    // Linear 1..10 의 9 m = 1/9, 클립 0.5, 스테레오 밸런스 가운데 1 → 0.0556.
    const float32 before = AudioSceneTestInternal::renderMean( engine, 0 );
    SW_EXPECT_NEAR_EQUAL( 0.5f / 9.0f, before, 1e-3f );

    manager.destroyObject( pSource );
    manager.tick( 1.0f / 60.0f );
    // 오브젝트가 사라질 때 끝 처리(`_bStopOnEndPlay`)는 플레이를 시작하지 않은 매니저라 돌지 않는다 — 소리는 이어진다.
    const float32 after = AudioSceneTestInternal::renderMean( engine, 0 );
    SW_EXPECT_NEAR_EQUAL( before, after, 1e-3f );
}
