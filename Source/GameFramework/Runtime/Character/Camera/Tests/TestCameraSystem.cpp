#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Character/Camera/CameraSystem.h"

using namespace sw;

// CameraSystemTest — 디렉터 · 매니저의 미룬 포즈 계산이 시스템 한 곳에서 풀리는지, 매니저가 레지스트리에 오르는지(nogpu, 물리 없음).

SW_TEST_CASE( CameraSystemTest, SystemRunsRegisteredCameraComponents )
{
    GameObjectManager manager;
    SW_EXPECT_TRUE( CameraSystem::find( manager ) == nullptr );

    GameObject* pRig = manager.createGameObject( hashed_string( "Rig" ) );
    SW_REQUIRE_NOT_NULL( pRig );
    (void)pRig->addComponent<CameraComponent>();
    CameraDirectorComponent* pDirector = pRig->addComponent<CameraDirectorComponent>();
    SW_REQUIRE_NOT_NULL( pDirector );
    SW_REQUIRE_TRUE( CameraSystem::find( manager ) != nullptr ); // 첫 카메라 컴포넌트가 등록될 때 붙는다

    GameObject* pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    SW_REQUIRE_NOT_NULL( pPlayer );
    (void)pPlayer->addComponent<CameraComponent>();
    CameraManagerComponent* pCameraManager = pPlayer->addComponent<CameraManagerComponent>();
    SW_REQUIRE_NOT_NULL( pCameraManager );
    // 매니저는 컴포넌트 레지스트리에 오른다 — 예전에는 오르지 않아 `isViewTarget` 이 늘 false 였다.
    pCameraManager->setViewTarget( pRig->getHandle() );
    SW_EXPECT_TRUE( CameraManagerComponent::isViewTarget( manager, pRig->getHandle() ) );

    // 틱 밖의 갱신은 바로 푼다 — 시스템에 남길 것이 없다.
    pDirector->updateCamera( 0.0f );
    pCameraManager->updateCamera( 0.0f );
    SW_EXPECT_FALSE( pDirector->isResolvePending() );
    SW_EXPECT_FALSE( pCameraManager->isResolvePending() );
    CameraSystem::find( manager )->runFramePass( manager ); // 미룬 것이 없으면 아무것도 하지 않고 지난다
    SW_EXPECT_FALSE( pCameraManager->isResolvePending() );
}

/** @brief [CameraSystemTest] 화면 시점을 모는 디렉터(게임 시점 카메라 또는 뷰 타깃)는 씬에 하나다 — 캡처 카메라의 디렉터는 세지 않고, 화면을 모는 것이 둘이면 오류와 nullptr 이다 */
SW_TEST_CASE( CameraSystemTest, ViewDirectorIsUnique )
{
    SW_EXPECT_LOG( "ComponentRegistry.DuplicateUnique" );
    GameObjectManager manager;
    SW_EXPECT_NULL( CameraDirectorComponent::findViewDirector( manager ) );

    GameObject* pGameRig = manager.createGameObject( hashed_string( "GameRig" ) );
    SW_REQUIRE_NOT_NULL( pGameRig );
    pGameRig->addComponent<CameraComponent>()->setRole( CameraRole::Game );
    CameraDirectorComponent* pGameDirector = pGameRig->addComponent<CameraDirectorComponent>();
    SW_REQUIRE_NOT_NULL( pGameDirector );

    GameObject* pCaptureRig = manager.createGameObject( hashed_string( "CaptureRig" ) );
    SW_REQUIRE_NOT_NULL( pCaptureRig );
    pCaptureRig->addComponent<CameraComponent>()->setRole( CameraRole::Capture );
    SW_REQUIRE_NOT_NULL( pCaptureRig->addComponent<CameraDirectorComponent>() );
    SW_EXPECT_TRUE( CameraDirectorComponent::findViewDirector( manager ) == pGameDirector ); // 캡처 카메라는 화면을 몰지 않는다

    GameObject* pSecondRig = manager.createGameObject( hashed_string( "SecondGameRig" ) );
    SW_REQUIRE_NOT_NULL( pSecondRig );
    pSecondRig->addComponent<CameraComponent>()->setRole( CameraRole::Game );
    SW_REQUIRE_NOT_NULL( pSecondRig->addComponent<CameraDirectorComponent>() );
    SW_EXPECT_NULL( CameraDirectorComponent::findViewDirector( manager ) );
}

/** @brief [CameraSystemTest] 카메라 매니저 없이 디렉터만 있는 씬도 흔들림 채널을 듣는다. 그 카메라를 보는 매니저가 생기면 매니저만 더하고, 플레이가 끝나면 듣지 않는다 */
SW_TEST_CASE( CameraSystemTest, DirectorAloneHearsTheShakeChannel )
{
    GameObjectManager manager;
    GameObject*       pRig    = manager.createGameObject( hashed_string( "Rig" ) );
    CameraComponent*  pCamera = pRig->addComponent<CameraComponent>();
    pCamera->setRole( CameraRole::Game );
    CameraDirectorComponent* pDirector = pRig->addComponent<CameraDirectorComponent>();
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    pDirector->requestViewOverride( CameraViewOverride{
        float3{ 0.0f, 2.0f, 0.0f },
        float3{},
        1.0f, 100.0f
    } ); // 켠 시점이 있어야 디렉터가 푼다 — 틱 없이 부르는 동안 요청이 이어진다
    pDirector->updateCamera( 0.0f );
    const float3 restPosition = CameraPose::makePoseFromCamera( *pCamera )._position;

    CameraShakeRequest request;
    request._origin    = restPosition;
    request._amplitude = 1.0f;
    request._duration  = 1.0f;
    request._frequency = 30.0f;
    manager.getCameraRegistry().getShakeChannel().broadcast( request );
    pDirector->updateCamera( 0.05f );
    SW_EXPECT_TRUE( ( CameraPose::makePoseFromCamera( *pCamera )._position - restPosition ).getLength() > 0.1f );

    GameObject* pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    (void)pPlayer->addComponent<CameraComponent>();
    CameraManagerComponent* pCameraManager = pPlayer->addComponent<CameraManagerComponent>();
    SW_REQUIRE_NOT_NULL( pCameraManager );
    pCameraManager->setViewTarget( pRig->getHandle() );
    pDirector->updateCamera( 0.0f );
    SW_EXPECT_NEAR( 0.0f, ( CameraPose::makePoseFromCamera( *pCamera )._position - restPosition ).getLength(), 1.0e-4f ); // 매니저가 자기 몫으로 더한다

    pCameraManager->setViewTarget( GameObjectHandle{} );
    pRig->endPlay();
    manager.getCameraRegistry().getShakeChannel().broadcast( request );
    pDirector->updateCamera( 0.05f );
    SW_EXPECT_NEAR( 0.0f, ( CameraPose::makePoseFromCamera( *pCamera )._position - restPosition ).getLength(), 1.0e-4f );
}

/** @brief [CameraSystemTest] 상황(조준) 중에 뜬 카메라를 되살려도 고른 프리셋과 켜진 상황이 남는다 — 상황을 끄면 고른 프리셋으로 돌아온다(예전에는 조준 프리셋이 고른 것이 되어 남았다) */
SW_TEST_CASE( CameraSystemTest, SelectedPresetSurvivesRestoreInsideAContext )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "aim.cameras.xml" ), R"(<CameraPresetCatalog>
    <DefaultBlend curve="SmoothStep" duration="0.5"/>
    <Context name="Aim" priority="10" action="Aim"><Map from="orbit" to="orbit_aim"/></Context>
    <Preset id="orbit"><View mode="Orbit" pitch="24.999867" yaw="0" distance="12" offset="0,1.5,0"/></Preset>
    <Preset id="orbit_aim"><View mode="Orbit" pitch="10.000004" yaw="0" distance="4" offset="0,1.5,0"/></Preset>
</CameraPresetCatalog>)" ) );
    StringBuilder<constant::buffer::kMax512> text;
    text.appendFormat( "<CameraDirectorComponent _catalogPath=\"%#\" _initialPreset=\"orbit\"/>", test::ScopedScratchDomain::makeID( "aim.cameras.xml" ).str() );
    const auto addDirector = [&text]( GameObjectManager& manager )
    {
        GameObject* pRig = manager.createGameObject( hashed_string( "Rig" ) );
        (void)pRig->addComponent<CameraComponent>();
        CameraDirectorComponent* pDirector = pRig->addComponent<CameraDirectorComponent>();
        SW_EXPECT_TRUE( XMLSerializer::deserialize( pDirector, *CameraDirectorComponent::StaticType(), text.view() ) );
        return pDirector;
    };
    GameObjectManager        manager;
    CameraDirectorComponent* pDirector = addDirector( manager );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->setContext( hashed_string( "Aim" ), true ) );
    SW_REQUIRE_TRUE( pDirector->getActivePresetID() == hashed_string( "orbit_aim" ) );
    Archive archive;
    StateIO::writeObject( *pDirector, archive );
    vector<uint8> bytes;
    archive.writeData( bytes );

    GameObjectManager        restoredManager;
    CameraDirectorComponent* pRestored = addDirector( restoredManager );
    pRestored->restoreState( std::move( bytes ) );
    restoredManager.beginPlay();
    SW_EXPECT_TRUE( pRestored->getActivePresetID() == hashed_string( "orbit_aim" ) );
    SW_EXPECT_TRUE( pRestored->getSelectedPresetID() == hashed_string( "orbit" ) );
    SW_EXPECT_TRUE( pRestored->isContextActive( hashed_string( "Aim" ) ) );
    SW_REQUIRE_TRUE( pRestored->setContext( hashed_string( "Aim" ), false ) );
    SW_EXPECT_TRUE( pRestored->getActivePresetID() == hashed_string( "orbit" ) );
    manager.endPlay();
    restoredManager.endPlay();
}
