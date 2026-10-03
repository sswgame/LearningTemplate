#include "pch.h"

// 미리보기 메타(`EditorPreview`)는 에디터 메타데이터라 Shipping 빌드에는 없다 — 이 시험도 없다.

#if !defined( SW_SHIPPING )

    #include "Editor/Common/Commands/EditorViewportPreview.h"

    #include "Engine/Object/GameObject/GameObject.h"
    #include "Engine/Object/GameObject/GameObjectManager.h"
    #include "Engine/Reflection/ReflectionTypes.h"
    #include "Engine/Scene/Scene.h"
    #include "Engine/Scene/SceneManager.h"

    #include "EditorTest/EditorPreviewProbe.h"
    #include "EditorTest/EditorTestServices.h"
    #include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorViewportPreviewTest] 미리보기 메서드는 메타로 찾는다 — 이름만 같은 타입은 찾지 않는다
 * @details EditorModule 은 GameFramework 를 링크하지 않는다. 타입 이름(`"DialogueRunnerComponent"`)을 문자열로 비교해 찾으면 다른 모듈의
 *          컴포넌트는 미리보기를 받을 수 없고, 이름만 같은 타입은 받는다.
 */
SW_TEST_CASE( EditorViewportPreviewTest, FindsPreviewMethodByMetaNotByTypeName )
{
    const TypeInfo* pProbeType = editortest::EditorPreviewProbeComponent::StaticType();
    SW_ASSERT_NOT_NULL( pProbeType );
    const FunctionInfo* pMethod = EditorViewportPreview::findPreviewMethod( *pProbeType, EditorViewportPreview::kDialogueLinePreview );
    SW_ASSERT_NOT_NULL( pMethod );
    SW_EXPECT_STREQ( "showPreviewLine", pMethod->_name.c_str() );
    SW_EXPECT_NULL( EditorViewportPreview::findPreviewMethod( *pProbeType, "SomethingElse" ) );

    const TypeInfo* pNamesakeType = editortest::DialogueRunnerComponent::StaticType();
    SW_ASSERT_NOT_NULL( pNamesakeType );
    SW_EXPECT_STREQ( "DialogueRunnerComponent", pNamesakeType->_name.c_str() );
    SW_EXPECT_NULL( EditorViewportPreview::findPreviewMethod( *pNamesakeType, EditorViewportPreview::kDialogueLinePreview ) );
}

/**
 * @brief [EditorViewportPreviewTest] 대사 미리보기는 메타를 단 컴포넌트에만 닿는다
 */
SW_TEST_CASE( EditorViewportPreviewTest, DialogueLineReachesMetaTaggedComponentOnly )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "PreviewProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager*        pManager = pScene->getObjectManager();

    GameObject*                              pProbeObject = pManager->createGameObject( hashed_string( "Narrator" ) );
    editortest::EditorPreviewProbeComponent* pProbe       = pProbeObject->addComponent<editortest::EditorPreviewProbeComponent>();
    GameObject*                              pOtherObject = pManager->createGameObject( hashed_string( "Bystander" ) );
    editortest::DialogueRunnerComponent*     pNamesake    = pOtherObject->addComponent<editortest::DialogueRunnerComponent>();
    SW_ASSERT_NOT_NULL( pProbe );
    SW_ASSERT_NOT_NULL( pNamesake );

    EditorViewportPreview::applyDialogueLine( "Hero", "Hello there" );

    SW_EXPECT_EQUAL( 1, pProbe->_previewCount );
    SW_EXPECT_STREQ( "Hero", pProbe->_lastSpeaker.c_str() );
    SW_EXPECT_STREQ( "Hello there", pProbe->_lastText.c_str() );
    SW_EXPECT_EQUAL( 0, pNamesake->_previewCount );
}

#endif
