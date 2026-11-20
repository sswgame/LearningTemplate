// RequiredComponentTest — 필요 컴포넌트(`Required<T>`)는 등록 때 한 번 묶고 핸들로 읽는다. 없으면 등록 자리에서 오류다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Water/BuoyancyComponent.h"
#include "Engine/Object/Component/RequiredComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/**
 * @brief [RequiredComponentTest] 묶은 필요 컴포넌트는 목록을 돌지 않고 핸들로 찾고, 지운 뒤에는 매달린 포인터 대신 nullptr 이다
 */
SW_TEST_CASE( RequiredComponentTest, BoundComponentResolvesByHandle )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Tagged" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    TagComponent* pTag = pObject->addComponent<TagComponent>();
    SW_REQUIRE_NOT_NULL( pTag );

    Required<TagComponent> required;
    SW_REQUIRE_TRUE( required.bind( *pTag ) );
    SW_EXPECT_TRUE( required.find() == pTag );

    SW_REQUIRE_TRUE( pObject->removeComponent( pTag ) );
    manager.processDeferredDestruction();
    SW_EXPECT_NULL( required.find() );
}

/**
 * @brief [RequiredComponentTest] 필요 컴포넌트가 없는 오브젝트에 붙이면 등록에서 오류 로그가 남는다 — 물리 단계마다 조용히 건너뛰던 빠진 의존이 드러난다
 * @details 변이: `Component::dispatchRegister` 의 `bindRequiredComponents` 호출을 빼면 로그 기대가 진다.
 */
SW_TEST_CASE( RequiredComponentTest, MissingComponentIsReportedAtRegister )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "LonelyFloat" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    SW_EXPECT_LOG( "Component.RequiredComponentMissing" );
    SW_EXPECT_NOT_NULL( pObject->addComponent<BuoyancyComponent>() );
}
