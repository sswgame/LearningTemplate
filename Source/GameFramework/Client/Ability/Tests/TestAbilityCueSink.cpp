// AbilityCueSinkTest — 클라이언트의 어빌리티 큐 창구는 서비스로 찾히고, 프리팹도 소리도 없는 큐는 오브젝트를 만들지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/SceneIterationTestAccess.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IAbilityCueSink.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

using namespace sw;

/** @brief [AbilityCueSinkTest] 빈 큐는 아무것도 스폰하지 않고 씬의 오브젝트는 그대로다 */
SW_TEST_CASE( AbilityCueSinkTest, EmptyCueSpawnsNothing )
{
    IAbilityCueSink* const pSink = game::findSubsystem<IAbilityCueSink>();
    SW_REQUIRE_NOT_NULL( pSink );
    GameObjectManager manager;
    (void)manager.createGameObject( hashed_string( "Caster" ) );
    manager.beginPlay();
    const size_t           countBefore = manager.getAllGameObjects( SceneIterationTestAccess::getKey() ).size();
    AbilityPresentationDef cue;
    cue._cueTag = TagID::request( "AbilityCueSinkTest.Empty" );
    pSink->presentCue( manager, cue, float3{ 1.0f, 2.0f, 3.0f } );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( countBefore, manager.getAllGameObjects( SceneIterationTestAccess::getKey() ).size() );
}
