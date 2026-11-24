// TagGateTest — 태그 문: 요구 태그를 가진 켜진 오브젝트가 가까이 있을 때만 열린다
#include "pch.h"

#include "Core/Name/TagID.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeInfo.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/TagGateComponent.h"

using namespace sw;

/** @brief [TagGateTest] 꺼진 것(주운 열쇠 · 풀에 돌아간 것)은 태그 색인에 남아도 문을 열지 않고, 켜면 연다 */
SW_TEST_CASE( TagGateTest, InactiveObjectDoesNotOpenTheGate )
{
    GameObjectManager manager;
    GameObject* const pGateObject = manager.createGameObject( hashed_string( "Gate" ) );
    GameObject* const pKey        = manager.createGameObject( hashed_string( "Key" ) );
    (void)pGateObject->addComponent<SceneComponent>();
    (void)pKey->addComponent<SceneComponent>();
    pKey->addTag( "Test.Key"_tag );
    TagGateComponent* const   pGate         = pGateObject->addComponent<TagGateComponent>();
    const PropertyInfo* const pRequiredTags = TagGateComponent::StaticType()->findProperty( hashed_string( "_requiredTags" ) );
    SW_REQUIRE_NOT_NULL( pRequiredTags );
    static_cast<TagContainer*>( pRequiredTags->getRawPtr( static_cast<void*>( pGate ) ) )->addTag( "Test.Key"_tag );
    manager.beginPlay();

    pKey->setActive( false );
    pGate->stepOnce();
    SW_EXPECT_FALSE( pGate->isOpen() );
    pKey->setActive( true );
    pGate->stepOnce();
    SW_EXPECT_TRUE( pGate->isOpen() );
}
