/**
 * @file TestComponentStableKey.cpp
 * @brief 컴포넌트 안정 키 — 씬 파일의 부착 대상과 에디터 선택 복원이 같은 키로 컴포넌트를 되찾는다.
 */
#include "pch.h"

#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [ComponentStableKeyTest] 같은 타입이 둘이면 몇 번째인지로 갈리고, 키는 되찾기와 왕복한다
 */

SW_TEST_CASE( ComponentStableKeyTest, KeyRoundTripsThroughFind )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "KeyOwner" ) );
    SW_ASSERT_TRUE( pObj != nullptr );

    sw::SceneComponent* pFirst  = pObj->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pSecond = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pFirst != nullptr && pSecond != nullptr );

    const sw::string firstKey  = sw::ComponentStableKey::makeKey( pFirst );
    const sw::string secondKey = sw::ComponentStableKey::makeKey( pSecond );
    SW_EXPECT_TRUE( firstKey.empty() == false );
    SW_EXPECT_TRUE( firstKey != secondKey );
    SW_EXPECT_TRUE( firstKey.find( "#0" ) != sw::string::npos );
    SW_EXPECT_TRUE( secondKey.find( "#1" ) != sw::string::npos );

    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, firstKey ) == pFirst );
    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, secondKey ) == pSecond );
}

/**
 * @brief [ComponentStableKeyTest] 이름이 붙은 컴포넌트는 타입 대신 이름으로 세고, 무명 형제의 번호에 끼지 않는다
 */
SW_TEST_CASE( ComponentStableKeyTest, NamedComponentCountsApart )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "KeyOwner" ) );
    SW_ASSERT_TRUE( pObj != nullptr );

    sw::SceneComponent* pNamed = pObj->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pPlain = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pNamed != nullptr && pPlain != nullptr );
    pNamed->setComponentName( sw::hashed_string( "Muzzle" ) );

    SW_EXPECT_EQUAL( sw::string{ "Muzzle#0" }, sw::ComponentStableKey::makeKey( pNamed ) );
    SW_EXPECT_TRUE( sw::ComponentStableKey::makeKey( pPlain ).find( "#0" ) != sw::string::npos );
    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, "Muzzle#0" ) == pNamed );
}

/**
 * @brief [ComponentStableKeyTest] 형식이 아닌 키 · 없는 키 · 소유자 없는 컴포넌트는 조용히 비어 있다
 */
SW_TEST_CASE( ComponentStableKeyTest, MalformedAndMissingKeysAreSafe )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "KeyOwner" ) );
    SW_ASSERT_TRUE( pObj != nullptr );
    sw::SceneComponent* pScene = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pScene != nullptr );

    const sw::string key = sw::ComponentStableKey::makeKey( pScene );
    SW_ASSERT_TRUE( key.size() > 2 );
    const sw::string baseOnly = key.substr( 0, key.size() - 2 );

    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, "" ) == nullptr );
    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, baseOnly ) == nullptr );        // '#' 가 없다
    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, baseOnly + "#" ) == nullptr );  // 번호가 없다
    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, baseOnly + "#x" ) == nullptr ); // 숫자가 아니다
    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, baseOnly + "#7" ) == nullptr ); // 그 번호까지 없다
    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( pObj, "Nope#0" ) == nullptr );
    SW_EXPECT_TRUE( sw::ComponentStableKey::findComponent( nullptr, key ) == nullptr );
    SW_EXPECT_TRUE( sw::ComponentStableKey::makeKey( nullptr ).empty() );
}
