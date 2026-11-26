// CityGameModeTest — 도시 디렉터의 명령(짓기 · 허물기 · 시간 · 빨리 감기)은 판이 열린 동안만 받고, 도시 카탈로그가 없으면 판을 열지 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityGameModeComponent.h"

using namespace sw;

/** @brief [CityGameModeTest] 판이 열리기 전의 빨리 감기 명령은 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( CityGameModeTest, FastForwardBeforeTheTownOpensIsRefused )
{
    GameObjectManager      manager;
    CityGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<CityGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitFastForward( true ) );
    SW_EXPECT_EQUAL( 0u, pGameMode->getLayoutRevision() ); // 뷰는 맞출 배치가 없다
}

/** @brief [CityGameModeTest] 판이 열리기 전의 짓기 · 허물기 · 시간 명령은 거절한다 — 배치 판과 센 수가 그대로다 */
SW_TEST_CASE( CityGameModeTest, BuildCommandsBeforeTheTownOpensAreRefused )
{
    GameObjectManager             manager;
    CityBuilderGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<CityGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_TRUE( pGameMode->submitPlaceRoad( int2{ 0, 0 } ) == CityPlaceResult::OutOfBounds );
    SW_EXPECT_TRUE( pGameMode->submitPlaceBuilding( hashed_string( "house" ), int2{ 0, 0 } ) == CityPlaceResult::OutOfBounds );
    SW_EXPECT_FALSE( pGameMode->submitDemolish( int2{ 0, 0 } ) );
    SW_EXPECT_FALSE( pGameMode->submitTimeInput( true, false, false ) );
    SW_EXPECT_FALSE( pGameMode->submitAutoPlanToggle() );
    SW_EXPECT_EQUAL( 0u, pGameMode->getLayoutRevision() );
    SW_EXPECT_EQUAL( uint64{ 0 }, pGameMode->getBuildCount().get() );
    SW_EXPECT_EQUAL( uint64{ 0 }, pGameMode->getDemolishCount().get() );
    SW_EXPECT_NULL( pGameMode->findCityWallet() ); // 플레이어 상태를 가리키지 않았다
}

/** @brief [CityGameModeTest] 씬이 도시 카탈로그를 적지 않았으면 오류를 남기고 판을 열지 않는다 — C++ 기본값으로 열지 않는다 */
SW_TEST_CASE( CityGameModeTest, TownWithoutCityDataDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    SW_EXPECT_LOG( "ObjectValidation.RequiredEmptyAtBeginPlay" ); // 랜드마크 건물(필수 칸)도 적지 않은 씬이다
    GameObjectManager      manager;
    CityGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<CityGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    SW_EXPECT_FALSE( pGameMode->submitFastForward( true ) );
    SW_EXPECT_EQUAL( 0u, pGameMode->getLayoutRevision() );
    manager.endPlay();
}
