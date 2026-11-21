#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Foliage/WindComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

// 컴포넌트 레지스트리의 "씬에 하나" 찾기(`ComponentRegistry::findUnique`) — 등록 순서로 첫 것을 고르지 않고 둘째를 오류로 본다(nogpu, 헤드리스).
// 등록하는 컴포넌트는 아무 것이나 되므로 바람(`WindComponent`)을 쓴다.

using namespace sw;

/** @brief [ComponentRegistryUniqueTest] 없으면 nullptr, 하나면 그것, 셀 조건을 넘지 못한 것(꺼진 바람)은 세지 않고, 조건을 넘은 것이 둘이면 오류와 nullptr 이다 */
SW_TEST_CASE( ComponentRegistryUniqueTest, SecondCountedComponentIsAnError )
{
    SW_EXPECT_LOG( "ComponentRegistry.DuplicateUnique" );
    GameObjectManager        manager;
    const ComponentRegistry& registry  = manager.getComponentRegistry();
    const auto               isEnabled = []( const WindComponent& wind )
    { return wind.isActive(); };
    SW_EXPECT_NULL( registry.findUnique<WindComponent>( isEnabled ) );

    WindComponent* pFirst = manager.createGameObject( hashed_string( "WindA" ) )->addComponent<WindComponent>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_EXPECT_TRUE( registry.findUnique<WindComponent>( isEnabled ) == pFirst );

    WindComponent* pSecond = manager.createGameObject( hashed_string( "WindB" ) )->addComponent<WindComponent>();
    SW_REQUIRE_NOT_NULL( pSecond );
    pSecond->setActive( false );
    SW_EXPECT_TRUE( registry.findUnique<WindComponent>( isEnabled ) == pFirst ); // 꺼진 둘째는 세지 않는다
    SW_EXPECT_NULL( registry.findUnique<WindComponent>() );                      // 조건 없이 세면 둘이다

    pSecond->setActive( true );
    SW_EXPECT_NULL( registry.findUnique<WindComponent>( isEnabled ) );
    WindSettings settings;
    SW_EXPECT_FALSE( WindComponent::findWind( manager, settings ) ); // 씬의 바람도 같은 규칙이다
}
