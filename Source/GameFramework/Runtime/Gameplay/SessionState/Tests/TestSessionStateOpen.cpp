#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

using namespace sw;

/**
 * @brief [SessionStateOpenTest] 오브젝트에 붙지 않은(판 상태를 찾을 수 없는) 디렉터는 판을 열지 못하고 오류와 Refused 를 받는다
 */
SW_TEST_CASE( SessionStateOpenTest, MissingStateIsRefused )
{
    SW_EXPECT_LOG( "SessionState.MissingOnOwner" );
    SessionStateComponent  loose;
    SessionStateInitResult result = SessionStateInitResult::Fresh;

    SW_EXPECT_TRUE( SessionStateComponent::openOnOwner( loose, SessionStateSettings{}, result ) == nullptr );
    SW_EXPECT_TRUE( result == SessionStateInitResult::Refused );
    SW_EXPECT_TRUE( SessionStateComponent::openOnOwner( loose, SessionStateSettings{} ) == nullptr );
}

/**
 * @brief [SessionStateOpenTest] 월드 상태는 씬에 하나다 — 같은 씬의 둘째 판 상태는 열리지 않고, 찾는 쪽(`findOpen`)은 첫 판만 본다(하위 레벨 미니게임이 바탕 씬에 판 상태를 열어도 시계 · 날씨가 갈리지 않는다)
 */
SW_TEST_CASE( SessionStateOpenTest, SecondStateInSceneIsRefused )
{
    SW_EXPECT_LOG( "SessionState.SecondInScene" );
    GameObjectManager            manager;
    SessionStateComponent* const pFirst  = manager.createGameObject( hashed_string( "Town" ) )->addComponent<SessionStateComponent>();
    SessionStateComponent* const pSecond = manager.createGameObject( hashed_string( "CardTable" ) )->addComponent<SessionStateComponent>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    SW_REQUIRE_TRUE( pFirst->initialize( SessionStateSettings{} ) == SessionStateInitResult::Fresh );
    SW_EXPECT_TRUE( pSecond->initialize( SessionStateSettings{} ) == SessionStateInitResult::Refused );
    SW_EXPECT_FALSE( pSecond->isInitialized() );
    SW_EXPECT_TRUE( SessionStateComponent::findOpen( &manager ) == pFirst );
}
