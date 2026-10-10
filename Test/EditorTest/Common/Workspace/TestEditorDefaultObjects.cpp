#include "pch.h"

#include "Editor/Common/Workspace/EditorDefaultObjects.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorDefaultObjectsTest] 기본 인스턴스(CDO)와 같은 값은 기본이고, 바꾸면 기본이 아니다. 메타 글은 숫자를 값으로 견준다("1" 과 "1.0")
 */
SW_TEST_CASE( EditorDefaultObjectsTest, DefaultInstanceAndMetadataComparison )
{
    EditorDefaultObjects defaults;
    const Component*     pDefault = defaults.findDefault( *CameraComponent::StaticType() );
    SW_ASSERT_TRUE( pDefault != nullptr );
    SW_EXPECT_TRUE( defaults.findDefault( *CameraComponent::StaticType() ) == pDefault ); // 한 번 만들어 둔다

    GameObjectManager   manager;
    GameObject*         pObject   = manager.createGameObject( hashed_string( "DefaultProbe" ) );
    CameraComponent*    pCamera   = pObject->addComponent<CameraComponent>();
    const PropertyInfo* pPriority = CameraComponent::StaticType()->findPropertyInHierarchy( hashed_string( "_priority" ) );
    SW_ASSERT_TRUE( pCamera != nullptr && pPriority != nullptr );
    SW_EXPECT_TRUE( EditorDefaultObjects::isDefaultValue( *pPriority, pCamera, pDefault ) );
    *pPriority->getValuePtr<int32>( pCamera ) = 4;
    SW_EXPECT_FALSE( EditorDefaultObjects::isDefaultValue( *pPriority, pCamera, pDefault ) );

    PropertyInfo metaOnly            = *pPriority;
    metaOnly._metadata._defaultValue = "4.0";
    SW_EXPECT_TRUE( EditorDefaultObjects::isDefaultValue( metaOnly, pCamera, nullptr ) );
    metaOnly._metadata._defaultValue = "";
    SW_EXPECT_TRUE( EditorDefaultObjects::isDefaultValue( metaOnly, pCamera, nullptr ) ); // 기본을 모르면 차이도 없다

    defaults.clear();
    SW_EXPECT_TRUE( defaults.findDefault( *CameraComponent::StaticType() ) != nullptr );
}
