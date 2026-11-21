// SkirmishGameModeFindTest — 감시 값 · 개발 명령이 부르는 스커미시 디렉터 찾기(`SkirmishGameModeComponent::findInScene`)는 씬에 하나를 지킨다(nogpu, 판을 열지 않는다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/StarSkirmish/SkirmishGameModeComponent.h"

using namespace sw;

/** @brief [SkirmishGameModeFindTest] 디렉터가 하나면 그것이고, 둘이면 등록 순서로 첫 것을 고르지 않고 오류와 nullptr 이다 */
SW_TEST_CASE( SkirmishGameModeFindTest, SecondDirectorIsAnError )
{
    SW_EXPECT_LOG( "ComponentRegistry.DuplicateUnique" );
    GameObjectManager          manager;
    SkirmishGameModeComponent* pFirst = manager.createGameObject( hashed_string( "DirectorA" ) )->addComponent<SkirmishGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_EXPECT_TRUE( SkirmishGameModeComponent::findInScene( manager ) == pFirst );
    SW_REQUIRE_NOT_NULL( manager.createGameObject( hashed_string( "DirectorB" ) )->addComponent<SkirmishGameModeComponent>() );
    SW_EXPECT_NULL( SkirmishGameModeComponent::findInScene( manager ) );
}
