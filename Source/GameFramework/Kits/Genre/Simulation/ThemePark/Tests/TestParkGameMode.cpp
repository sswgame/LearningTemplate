// ParkGameModeTest — 공원 디렉터의 명령은 판이 열린 동안만 받고, 배치 데이터가 없으면 판을 열지 않으며, 배치는 디렉터 오브젝트의 자리를 원점으로 놓인다(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Presentation/ISoundPlayer.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Component/ParkGameModeComponent.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/Tests/ThemeParkTestRules.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

using namespace sw;

namespace
{
    /** @brief 어떤 명령도 받지 않고 뷰가 맞출 공원도 없는지 봅니다. */
    void expectParkRefusesCommands( ParkGameModeComponent& gameMode )
    {
        SW_EXPECT_FALSE( gameMode.submitBuild( 0 ) );
        SW_EXPECT_FALSE( gameMode.submitBuildCheapest() );
        SW_EXPECT_FALSE( gameMode.submitToggleRideOpen( 0 ) );
        SW_EXPECT_FALSE( gameMode.submitRidePriceChange( 0, 1 ) );
        SW_EXPECT_FALSE( gameMode.submitEntryFeeChange( 1 ) );
        SW_EXPECT_EQUAL( 0u, gameMode.getParkRevision() );
    }
} // namespace

/** @brief [ParkGameModeTest] 판이 열리기 전의 명령은 거절한다 — 클라이언트가 먼저 돌아도 공원을 바꾸지 않는다 */
SW_TEST_CASE( ParkGameModeTest, CommandsBeforeTheParkOpensAreRefused )
{
    GameObjectManager      manager;
    ParkGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<ParkGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    expectParkRefusesCommands( *pGameMode );
}

/** @brief [ParkGameModeTest] 씬이 배치 데이터를 적지 않았으면 오류를 남기고 판을 열지 않는다 — C++ 기본값으로 열지 않는다 */
SW_TEST_CASE( ParkGameModeTest, ParkWithoutLayoutDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    GameObjectManager      manager;
    ParkGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<ParkGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    expectParkRefusesCommands( *pGameMode );
    manager.endPlay();
}

/** @brief [ParkGameModeTest] 배치 데이터는 디렉터 오브젝트의 자리 · 요를 원점으로 놓인다 — 하위 레벨로 얹은 공원이 얹은 자리에서 열리고, 열린 공원은 명령을 받는다 */
SW_TEST_CASE( ParkGameModeTest, LayoutIsPlacedAtTheDirectorObject )
{
    const test::ScopedScratchDomain           domain;
    StringBuilder<constant::buffer::kMax4096> layoutText;
    layoutText.appendFormat( "<ParkLayout gateX=\"0\" gateZ=\"10\" startingCash=\"14000\" entryFee=\"0\">%#"
                             "<FlatRide id=\"cups\" name=\"Cups\" excitement=\"2\" intensity=\"1\" nausea=\"1\" cycleTime=\"40\" capacity=\"16\" price=\"3\" runningCost=\"3\" cost=\"2000\" "
                             "x=\"45\" z=\"0\" entranceX=\"45\" entranceZ=\"-10\" shape=\"Cylinder\" sizeX=\"6\" sizeY=\"2\" sizeZ=\"6\" color=\"1,1,1,1\"/></ParkLayout>",
                             kParkRules );
    (void)domain.makeDiskFolder( "park/data" ); // 폴더만 만든다
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "park/data/coasters.xml" ), kParkCoasters ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "park/data/rides.xml" ), layoutText.view() ) );

    GameObjectManager     manager;
    GameObject* const     pDirector = manager.createGameObject( hashed_string( "Director" ) );
    SceneComponent* const pOrigin   = pDirector->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pOrigin );
    pOrigin->setLocalPosition( float3{ 100.0f, 3.0f, 0.0f } );
    pOrigin->setLocalRotation( float3{ 0.0f, MathUtil::toRadian( 90.0f ), 0.0f } );
    ParkGameModeComponent* const pGameMode = pDirector->addComponent<ParkGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    const PropertyInfo* const pPath = ParkGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( "_parkDataPath" ) );
    SW_REQUIRE_NOT_NULL( pPath );
    SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( *pPath, MutableObjectAddress{ pGameMode }, test::ScopedScratchDomain::makeID( "park/data/rides.xml" ).str(),
                                                        SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
    if ( game::findSubsystem<ISoundPlayer>() != nullptr )
        SW_EXPECT_LOG( "AudioEngine.UnknownAudioEvent" ); // 지은 소리를 내려 한다 — 이벤트 라이브러리는 게임이 설 때 싣는다
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, pGameMode->getPlacements().size() );

    // 요 90 도 — 앞(+Z)이 +X 로, +X 가 -Z 로 간다.
    constexpr float32                           kTolerance = 0.001f;
    const ParkGameModeComponent::RidePlacement& placement  = pGameMode->getPlacements()[0];
    SW_EXPECT_NEAR( 100.0f, placement._position._x, kTolerance );
    SW_EXPECT_NEAR( 3.0f, placement._position._y, kTolerance );
    SW_EXPECT_NEAR( -45.0f, placement._position._z, kTolerance );
    SW_EXPECT_NEAR( 90.0f, placement._ride._entrance._x, kTolerance );
    SW_EXPECT_NEAR( -45.0f, placement._ride._entrance._z, kTolerance );
    SW_EXPECT_NEAR( 110.0f, pGameMode->getSimulation().getSettings()._gatePosition._x, kTolerance );
    SW_EXPECT_NEAR( 3.0f, pGameMode->getSimulation().getSettings()._gatePosition._y, kTolerance );

    // 처음에 가장 싼 평지 놀이기구를 지었다 — 열린 공원은 명령을 받는다.
    SW_EXPECT_EQUAL( 1u, pGameMode->getParkRevision() );
    SW_EXPECT_EQUAL( 0, placement._rideIndex );
    SW_EXPECT_FALSE( pGameMode->submitBuild( 0 ) ); // 이미 지었다
    SW_EXPECT_TRUE( pGameMode->submitToggleRideOpen( 0 ) );
    SW_EXPECT_TRUE( pGameMode->getSimulation().getRides()[0]._bOpen == SW_FALSE );
    SW_EXPECT_FALSE( pGameMode->submitToggleRideOpen( 1 ) );
    SW_EXPECT_TRUE( pGameMode->submitEntryFeeChange( 5 ) );
    SW_EXPECT_EQUAL( 5, pGameMode->getSimulation().getSettings()._entryFee );

    // 쓴 상태를 되돌리면 그때의 공원이다.
    Archive written;
    pGameMode->writeState( written );
    SW_EXPECT_TRUE( pGameMode->submitToggleRideOpen( 0 ) ); // 상태를 쓴 뒤 연다
    SW_REQUIRE_TRUE( pGameMode->getSimulation().getRides()[0]._bOpen == SW_TRUE );
    pGameMode->restoreState( vector<uint8>( written.getData(), written.getData() + written.getSize() ) );
    SW_EXPECT_TRUE( pGameMode->getSimulation().getRides()[0]._bOpen == SW_FALSE ); // 쓴 때의 닫힘으로 돌아왔다
    SW_EXPECT_EQUAL( 2u, pGameMode->getParkRevision() );
    manager.endPlay();
}
