// GameModeViewTest — 디렉터 뷰의 공통 몫(`GameModeViewComponent`): 열린 판에서만 갱신하고, 디렉터가 없으면 플레이가 시작될 때 오류다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/GameModeTestComponent.h"

namespace sw
{
    /** @brief 갱신이 온 수와 그때의 라운드를 적는 뷰입니다. */
    REFLECT( HideInMenu )
    struct GameModeViewTestComponent : public GameModeViewComponent
    {
        REFLECT_BODY();

        GameModeViewTestComponent()
            : GameModeViewComponent( GameModeViewBinding::of<&GameModeViewTestComponent::updateView>() )
            , _updateCount{ 0 }
            , _seenRound{ 0 }
        {
        }

        void updateView( GameObjectManager& manager, GameModeTestComponent& gameMode )
        {
            (void)manager;
            ++_updateCount;
            _seenRound = gameMode._roundCount;
        }

        uint32 _updateCount;
        int32  _seenRound;
    };
} // namespace sw

using namespace sw;

/** @brief [GameModeViewTest] 같은 오브젝트의 디렉터가 열린 동안에만 갱신이 오고 그 디렉터를 받는다 */
SW_TEST_CASE( GameModeViewTest, UpdatesOnlyWhileTheGameIsOpen )
{
    GameObjectManager                manager;
    GameObject*                      pObject = manager.createGameObject( hashed_string( "Director" ) );
    const GameModeTestComponent*     pGame   = pObject->addComponent<GameModeTestComponent>();
    const GameModeViewTestComponent* pView   = pObject->addComponent<GameModeViewTestComponent>();
    SW_REQUIRE_TRUE( pGame != nullptr && pView != nullptr );
    manager.beginPlay();
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 1u, pView->_updateCount );
    SW_EXPECT_EQUAL( pGame->_roundCount, pView->_seenRound );
    manager.endPlay();
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 1u, pView->_updateCount );
}

/** @brief [GameModeViewTest] 디렉터가 없는 뷰는 플레이가 시작될 때 오류를 남기고 아무것도 하지 않는다 */
SW_TEST_CASE( GameModeViewTest, MissingGameModeIsAnErrorAtBeginPlay )
{
    GameObjectManager                manager;
    const GameModeViewTestComponent* pView = manager.createGameObject( hashed_string( "View" ) )->addComponent<GameModeViewTestComponent>();
    SW_REQUIRE_NOT_NULL( pView );
    SW_EXPECT_LOG( "GameMode.ViewWithoutGameMode" );
    manager.beginPlay();
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 0u, pView->_updateCount );
    manager.endPlay();
}

#include "TestGameModeView.cpp.gen.inl"
