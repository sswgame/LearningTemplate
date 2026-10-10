#include "pch.h"

#include "Editor/Common/Commands/EditorMultiEdit.h"

#include "EditorTest/EditorPreviewProbe.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorMultiEditTest] 공통 컴포넌트는 교집합이고 주 선택의 순서다. 혼합 판정 · 복사는 그 프로퍼티 하나만 본다
 */
SW_TEST_CASE( EditorMultiEditTest, CommonComponentsMixedValuesAndCopy )
{
    GameObjectManager manager;
    GameObject*       pFirst  = manager.createGameObject( hashed_string( "MultiFirst" ) );
    GameObject*       pSecond = manager.createGameObject( hashed_string( "MultiSecond" ) );
    SW_ASSERT_TRUE( pFirst != nullptr && pSecond != nullptr );
    CameraComponent* pFirstCamera  = pFirst->addComponent<CameraComponent>();
    CameraComponent* pSecondCamera = pSecond->addComponent<CameraComponent>();
    SW_ASSERT_TRUE( pFirstCamera != nullptr && pSecondCamera != nullptr );
    (void)pFirst->addComponent<editortest::EditorPreviewProbeComponent>(); // 첫 오브젝트에만 — 공통이 아니다

    vector<EditorMultiEditComponent> listCommon;
    EditorMultiEditUtil::collectCommonComponents( { pFirst, pSecond }, listCommon );
    SW_ASSERT_EQUAL( size_t{ 1 }, listCommon.size() );
    SW_EXPECT_TRUE( listCommon[0]._pType == CameraComponent::StaticType() );
    SW_ASSERT_EQUAL( size_t{ 2 }, listCommon[0]._listComponent.size() );

    const PropertyInfo* pPriority = CameraComponent::StaticType()->findPropertyInHierarchy( hashed_string( "_priority" ) );
    const PropertyInfo* pScale    = CameraComponent::StaticType()->findPropertyInHierarchy( hashed_string( "_resolutionScale" ) );
    SW_ASSERT_TRUE( pPriority != nullptr );
    *pPriority->getValuePtr<int32>( pFirstCamera )  = 3;
    *pPriority->getValuePtr<int32>( pSecondCamera ) = 1;
    SW_EXPECT_TRUE( EditorMultiEditUtil::hasMixedValues( *pPriority, { pFirstCamera, pSecondCamera } ) );

    SW_EXPECT_EQUAL( 1u, EditorMultiEditUtil::copyPropertyToOthers( *pPriority, listCommon[0]._listComponent ) );
    SW_EXPECT_EQUAL( 3, *pPriority->getValuePtr<int32>( pSecondCamera ) );
    SW_EXPECT_FALSE( EditorMultiEditUtil::hasMixedValues( *pPriority, { pFirstCamera, pSecondCamera } ) );
    if ( pScale != nullptr )
        SW_EXPECT_FALSE( EditorMultiEditUtil::hasMixedValues( *pScale, { pFirstCamera, pSecondCamera } ) );
}
