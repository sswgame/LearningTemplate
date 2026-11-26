// SkirmishGameModeTest — 1 대 1 디렉터: 찾기(`findInScene`)는 씬에 하나를 지키고, 명령은 판이 열린 동안만 받으며, 유닛 카탈로그가 없으면 판을 열지 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"

using namespace sw;

/** @brief [SkirmishGameModeTest] 디렉터가 하나면 그것이고, 둘이면 등록 순서로 첫 것을 고르지 않고 오류와 nullptr 이다 */
SW_TEST_CASE( SkirmishGameModeTest, SecondDirectorIsAnError )
{
    SW_EXPECT_LOG( "ComponentRegistry.DuplicateUnique" );
    GameObjectManager          manager;
    SkirmishGameModeComponent* pFirst = manager.createGameObject( hashed_string( "DirectorA" ) )->addComponent<SkirmishGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_EXPECT_TRUE( SkirmishGameModeComponent::findInScene( manager ) == pFirst );
    SW_REQUIRE_NOT_NULL( manager.createGameObject( hashed_string( "DirectorB" ) )->addComponent<SkirmishGameModeComponent>() );
    SW_EXPECT_NULL( SkirmishGameModeComponent::findInScene( manager ) );
}

/** @brief [SkirmishGameModeTest] 씬이 유닛 카탈로그를 적지 않았으면 오류를 남기고 판을 열지 않으며, 열리지 않은 판은 명령 · 속도 입력을 받지 않는다 */
SW_TEST_CASE( SkirmishGameModeTest, MatchWithoutUnitDataDoesNotOpenAndTakesNoCommand )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    GameObjectManager          manager;
    SkirmishGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<SkirmishGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    SW_EXPECT_EQUAL( uint64{ 0 }, pGameMode->getMatchID() );
    SW_EXPECT_EQUAL( 0, pGameMode->submitCommand( RTSCommand{} ) );
    SW_EXPECT_FALSE( pGameMode->submitTimeInput( true, false, false ) );
    manager.endPlay();
}
