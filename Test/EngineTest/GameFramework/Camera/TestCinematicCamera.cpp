#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "GameFramework/Base/Actor/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Actor/Camera/CameraManagerComponent.h"
#include "GameFramework/Base/Actor/Camera/CinematicCameraComponent.h"

#include "TestFramework/TestFramework.h"

// 시네마틱 카메라 — 시퀀서 카메라 신호(컷 프리셋 · 게임 ↔ 시네마틱 블렌드 · 대기 뷰 · 흔들림)와 값 트랙이 카메라 디렉터를 움직인다.

using namespace sw;

namespace
{
    struct CinematicCameraTestInternal
    {
        static CameraDirectorComponent* addDirectedCamera( GameObjectManager& manager, const utf8* pName, CameraRole role, const utf8* pXML )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            CameraComponent* pCamera = pObject->addComponent<CameraComponent>();
            if ( pCamera == nullptr )
                return nullptr;
            pCamera->setRole( role );
            CameraDirectorComponent* pDirector = pObject->addComponent<CameraDirectorComponent>();
            if ( pDirector == nullptr || pDirector->getCatalog().loadFromXMLText( pXML, pName ) == false )
                return nullptr;
            pDirector->setInputEnabled( false );
            return pDirector;
        }

        static SequenceCameraSignal makeSignal( SequenceCameraSignalKind kind, const utf8* pTarget, const utf8* pPreset = "" )
        {
            SequenceCameraSignal signal;
            signal._kind               = kind;
            signal._item._targetObject = pTarget;
            signal._item._preset       = pPreset;
            return signal;
        }
    };
} // namespace

/**
 * @brief [CinematicCameraTest] 컷은 대상 디렉터의 프리셋을 켜고, 시네마틱 시작은 게임 카메라 화면에서 섞어 들어가며, 끝은 마지막 컷 화면에서 게임 카메라로 섞어 나온다
 * @details 게임 카메라(+X 10)에서 컷 카메라의 wide 프리셋(+Y 20)으로 1 초 블렌드 인 — 0.5 초에 (5, 10, 0). 블렌드 없이 바꾸면 한 프레임에 20 m 를 뛴다.
 */
SW_TEST_CASE( CinematicCameraTest, CutsBlendInAndOutOfTheGameCamera )
{
    GameObjectManager        manager;
    CameraDirectorComponent* pGame = CinematicCameraTestInternal::addDirectedCamera( manager, "GameCam", CameraRole::Game, R"(<CameraPresets>
        <Preset id="game"><View mode="Fixed" offset="10,0,0"/></Preset>
    </CameraPresets>)" );
    CameraDirectorComponent* pShot = CinematicCameraTestInternal::addDirectedCamera( manager, "ShotA", CameraRole::Custom, R"(<CameraPresets>
        <Preset id="idle"><View mode="Fixed" offset="0,0,0"/></Preset>
        <Preset id="wide"><View mode="Fixed" offset="0,20,0"/><BlendIn curve="Cut"/></Preset>
    </CameraPresets>)" );
    SW_ASSERT_TRUE( pGame != nullptr && pShot != nullptr );
    GameObject* pSequenceObject = manager.createGameObject( hashed_string( "Sequence" ) );
    SW_ASSERT_NOT_NULL( pSequenceObject );
    CinematicCameraComponent* pCinematic = pSequenceObject->addComponent<CinematicCameraComponent>();
    SW_ASSERT_NOT_NULL( pCinematic );
    manager.beginPlay();

    pCinematic->handleCameraSignal( CinematicCameraTestInternal::makeSignal( SequenceCameraSignalKind::CutBegin, "ShotA", "wide" ) );
    SW_EXPECT_TRUE( pShot->getActivePresetID() == hashed_string( "wide" ) );
    SequenceCameraSignal begin = CinematicCameraTestInternal::makeSignal( SequenceCameraSignalKind::CinematicBegin, "ShotA" );
    begin._blendSeconds        = 1.0f;
    pCinematic->handleCameraSignal( begin );
    SW_EXPECT_TRUE( pCinematic->isCinematicActive() );
    SW_EXPECT_TRUE( pShot->isBlending() );
    pShot->updateCamera( 0.5f );
    const float3 halfway = pShot->getPose()._position;
    SW_EXPECT_NEAR_EQUAL( 10.0f, halfway._y, 0.5f ); // SmoothStep 가운데 — 게임 화면과 wide 의 절반
    SW_EXPECT_NEAR_EQUAL( 5.0f, halfway._x, 0.5f );
    pShot->updateCamera( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pShot->getPose()._position._y, 1.0e-3f );

    SequenceCameraSignal end = CinematicCameraTestInternal::makeSignal( SequenceCameraSignalKind::CinematicEnd, "ShotA" );
    end._blendSeconds        = 1.0f;
    pCinematic->handleCameraSignal( end );
    SW_EXPECT_FALSE( pCinematic->isCinematicActive() );
    SW_EXPECT_TRUE( pGame->isBlending() );
    pGame->updateCamera( 0.001f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pGame->getPose()._position._y, 0.1f ); // 마지막 컷 화면에서 출발한다
    pGame->updateCamera( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pGame->getPose()._position._x, 1.0e-3f );
    manager.endPlay();
}

/**
 * @brief [CinematicCameraTest] 대기 뷰 신호는 플레이어 카메라 매니저에 "곧 볼 시점" 을 등록하고 듣는 쪽에 알리며, 그 컷이 시작하면 푼다. 흔들림은 대상 카메라를 흔든다
 */
SW_TEST_CASE( CinematicCameraTest, PrewarmRegistersAPendingViewAndShakesMoveTheCamera )
{
    GameObjectManager        manager;
    CameraDirectorComponent* pShot = CinematicCameraTestInternal::addDirectedCamera( manager, "ShotB", CameraRole::Custom, R"(<CameraPresets>
        <Preset id="fixed"><View mode="Fixed" offset="0,2,0"/></Preset>
    </CameraPresets>)" );
    SW_ASSERT_NOT_NULL( pShot );
    GameObject* pPlayer = manager.createGameObject( hashed_string( "PlayerCamera" ) );
    SW_ASSERT_NOT_NULL( pPlayer );
    SW_ASSERT_NOT_NULL( pPlayer->addComponent<CameraComponent>() );
    CameraManagerComponent* pCameraManager = pPlayer->addComponent<CameraManagerComponent>();
    SW_ASSERT_NOT_NULL( pCameraManager );
    GameObject* pSequenceObject = manager.createGameObject( hashed_string( "Sequence" ) );
    SW_ASSERT_NOT_NULL( pSequenceObject );
    CinematicCameraComponent* pCinematic = pSequenceObject->addComponent<CinematicCameraComponent>();
    SW_ASSERT_NOT_NULL( pCinematic );
    manager.beginPlay();

    struct Listener
    {
        uint32  _count{ 0 };
        float32 _secondsUntil{ 0.0f };
        void    record( const CameraPendingView& view )
        {
            ++_count;
            _secondsUntil = view._secondsUntil;
        }
    };
    Listener     listener;
    Subscription subscription = pCameraManager->registerPendingViewListener( SW_DELEGATE_METHOD( OnCameraPendingViewDelegate, &Listener::record, &listener ) );

    SequenceCameraSignal prewarm = CinematicCameraTestInternal::makeSignal( SequenceCameraSignalKind::CutPrewarm, "ShotB" );
    prewarm._secondsUntil        = 0.25f;
    pCinematic->handleCameraSignal( prewarm );
    SW_EXPECT_TRUE( pCameraManager->hasPendingView() );
    SW_EXPECT_TRUE( pCameraManager->getPendingView()._camera == pShot->getOwner()->getHandle() );
    SW_EXPECT_EQUAL( 1u, listener._count );
    SW_EXPECT_NEAR_EQUAL( 0.25f, listener._secondsUntil, 1.0e-6f );

    pCinematic->handleCameraSignal( CinematicCameraTestInternal::makeSignal( SequenceCameraSignalKind::CutBegin, "ShotB" ) );
    SW_EXPECT_FALSE( pCameraManager->hasPendingView() ); // 그 시점이 화면에 왔다
    SW_EXPECT_EQUAL( 2u, listener._count );
    subscription.reset();

    SequenceCameraSignal shake = CinematicCameraTestInternal::makeSignal( SequenceCameraSignalKind::Shake, "ShotB" );
    shake._item._amplitude     = 0.5f;
    shake._durationSeconds     = 0.5f;
    pCinematic->handleCameraSignal( shake );
    float32 maxOffset = 0.0f;
    for ( int32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        pShot->updateCamera( 1.0f / 60.0f );
        maxOffset = MathUtil::max( maxOffset, MathUtil::abs( pShot->getPose()._position._x ) );
    }
    SW_EXPECT_TRUE_MSG( maxOffset > 0.05f, "시퀀서 흔들림이 카메라를 흔들지 않았습니다" );
    pShot->updateCamera( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pShot->getPose()._position._x, 1.0e-4f );
    manager.endPlay();
}

/**
 * @brief [CinematicCameraTest] 시퀀서 값 트랙(프로퍼티 트랙)이 디렉터의 시야각 덮어쓰기를 키로 몰면 프리셋 위에 그 시야각이 카메라에 쓰인다
 */
SW_TEST_CASE( CinematicCameraTest, ValueTrackDrivesTheDirectorFieldOfView )
{
    GameObjectManager        manager;
    CameraDirectorComponent* pShot = CinematicCameraTestInternal::addDirectedCamera( manager, "ShotC", CameraRole::Custom, R"(<CameraPresets>
        <Preset id="fixed"><View mode="Fixed"/><Lens fieldOfViewY="0.69813"/></Preset>
    </CameraPresets>)" );
    SW_ASSERT_NOT_NULL( pShot );
    manager.beginPlay();
    const CameraComponent* pCamera = pShot->getOwner()->getComponent<CameraComponent>();
    SW_ASSERT_NOT_NULL( pCamera );
    SW_EXPECT_NEAR_EQUAL( 40.0f * MathUtil::kDegreeToRadian, pCamera->getFieldOfViewY(), 1.0e-5f );

    SequenceAsset asset;
    asset._frameMin = 0;
    asset._frameMax = 20;
    SequenceKeyTrack track{};
    track._name         = "Zoom";
    track._targetObject = "ShotC";
    track._kind         = SequenceTrackKind::Property;
    track._propertyPath = "CameraDirectorComponent._fieldOfViewOverride";
    track.fitChannelsToKind();
    SW_ASSERT_TRUE( track.setChannelKey( 0, 0.0f, 0.5f ) );
    SW_ASSERT_TRUE( track.setChannelKey( 0, 10.0f, 1.0f ) );
    asset._listTrack.push_back( track );

    SequenceTimelineUtil::applyFrame( &manager, asset, 10 );
    pShot->updateCamera( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pCamera->getFieldOfViewY(), 1.0e-4f );
    manager.endPlay();
}
