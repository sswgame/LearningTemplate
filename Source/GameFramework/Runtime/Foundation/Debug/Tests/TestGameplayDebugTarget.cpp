#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Wind/WindComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/SceneIterationTestAccess.h"

#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"

// GameplayDebugTargetTest — 디버그 섹션의 대상 고르기: 대상을 정하지 않으면 그 컴포넌트를 가진 오브젝트 가운데 id 가 가장 작은 것이다(nogpu, 헤드리스 — 디버그 HUD 서비스가 없어 대상이 비었다).

using namespace sw;

/** @brief [GameplayDebugTargetTest] 대상이 없으면 그 컴포넌트를 가진 오브젝트 가운데 objectID 가 가장 작은 것을 본다 — 컴포넌트가 없는 오브젝트는 건너뛴다 */
SW_TEST_CASE( GameplayDebugTargetTest, FallbackIsTheLowestObjectIDWithTheComponent )
{
    GameObjectManager manager;
    SW_EXPECT_NULL( GameplayDebugTarget::findTargetOrLowestIDComponent<WindComponent>( SceneIterationTestAccess::getKey(), manager ) );

    GameObject* pBare   = manager.createGameObject( hashed_string( "Bare" ) );
    GameObject* pFirst  = manager.createGameObject( hashed_string( "First" ) );
    GameObject* pSecond = manager.createGameObject( hashed_string( "Second" ) );
    SW_REQUIRE_NOT_NULL( pBare );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    WindComponent* pSecondWind = pSecond->addComponent<WindComponent>(); // 붙이는 차례를 id 차례와 거꾸로 둔다
    WindComponent* pFirstWind  = pFirst->addComponent<WindComponent>();
    SW_REQUIRE_NOT_NULL( pSecondWind );
    SW_REQUIRE_NOT_NULL( pFirstWind );
    SW_REQUIRE_TRUE( pFirst->getObjectID() < pSecond->getObjectID() );

    SW_EXPECT_TRUE( GameplayDebugTarget::findTargetOrLowestIDComponent<WindComponent>( SceneIterationTestAccess::getKey(), manager ) == pFirstWind );
}
