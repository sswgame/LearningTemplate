#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "EditorTest/EditorPreviewProbe.h"
#include "EditorTest/EditorTestServices.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneObjectReference.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 새 프리팹의 루트(`Root`) 아래에 프리팹 인스턴스 자식(`Lamp`)이 있는 메인 씬과, 참조를 들고 있을 다른 씬(`Level`)입니다. */
    struct AdoptScene
    {
        SceneManager              _sceneManager;
        Scene*                    _pMain{ nullptr };
        Scene*                    _pLevel{ nullptr };
        EditorSelection           _selection;
        EditorWorkspace           _workspace{ &_selection };
        GameObject*               _pRoot{ nullptr };
        GameObject*               _pLamp{ nullptr };
        GameObject*               _pHolder{ nullptr };
        ScopedSceneManagerService _scopedScene{ _sceneManager };
        ScopedWorkspaceService    _scopedWorkspace{ _workspace };
        ScopedTypeRegistryService _scopedRegistry{ engine::getSubsystem<TypeRegistry>() }; // 참조 검사가 구조체 칸까지 내려가려고 쓴다

        AdoptScene()
        {
            _pMain  = _sceneManager.createEmptyActiveScene( "Main" );
            _pLevel = _sceneManager.createScene( "Level" );
            SW_ASSERT_TRUE( _pMain != nullptr && _pLevel != nullptr );
            _pMain->setSourcePath( "game/test/maps/main.scene.xml" );
            GameObjectManager& objects = *_pMain->getObjectManager();
            _pRoot                     = objects.createGameObject( hashed_string( "Root" ) );
            _pLamp                     = objects.createGameObject( hashed_string( "Lamp" ) );
            _pHolder                   = _pLevel->getObjectManager()->createGameObject( hashed_string( "Holder" ) );
            objects.mergePendingAdds();
            _pLevel->getObjectManager()->mergePendingAdds();
            _pRoot->addComponent<SceneComponent>();
            _pLamp->addComponent<SceneComponent>();
            SW_ASSERT_TRUE( _pLamp->attachToParent( _pRoot ) );
            _workspace.setPlacedPrefabPath( *_pLamp, "game/prefabs/lamp.prefab.xml" );
        }
    };
} // namespace

/**
 * @brief [EditorInspectorCommandsTest] 씬 편집이 막힌 동안 프리팹 명령은 이유를 경고로 남기고 거절한다
 * @details 막힘은 플레이 중이나 베이크 중 같다(`holdSceneEdits`). 조용히 반환하지 않고 `Editor.SceneEditRefused` 줄이 명령마다 하나 남는다.
 */
SW_TEST_CASE( EditorInspectorCommandsTest, PrefabCommandsRefuseWithAReasonWhileSceneEditsAreHeld )
{
    AdoptScene               fixture;
    ScopedPlaySessionService scopedPlaySession;
    EditorPlaySession::holdSceneEdits();
    SW_ASSERT_FALSE( EditorPlaySession::areSceneEditsAllowed() );

    test::ScopedLogCollector logs;
    SW_EXPECT_FALSE( EditorInspectorCommands::applyToPrefab( fixture._pLamp, sw::ResourceID{ "game/prefabs/lamp.prefab.xml" } ) );
    SW_EXPECT_FALSE( EditorInspectorCommands::revertToPrefab( fixture._pLamp, "game/prefabs/lamp.prefab.xml" ) );
    SW_EXPECT_FALSE( EditorInspectorCommands::unlinkPrefab( fixture._pLamp ) );
    SW_EXPECT_EQUAL( uint32( 3 ), logs.countWithID( "Editor.SceneEditRefused" ) );
    SW_EXPECT_FALSE( fixture._workspace.getPlacedPrefabPath( *fixture._pLamp ).empty() ); // 연결이 그대로다
}

/**
 * @brief [EditorInspectorCommandsTest] 참조 검사에 필요한 오브젝트를 얻지 못하면 검사를 건너뛰지 않고 거절한다
 */
SW_TEST_CASE( EditorInspectorCommandsTest, AdoptRejectsWhenReferenceCheckIsImpossible )
{
    const EditorInspectorCommands::AdoptResult result = EditorInspectorCommands::adoptChildPrefabInstances( nullptr );
    SW_EXPECT_TRUE( result.isRejected() );
    SW_EXPECT_EQUAL( uint32( 0 ), result._adoptedCount );
}

/**
 * @brief [EditorInspectorCommandsTest] 가리키는 참조가 없으면 자식 프리팹 인스턴스를 중첩 부품으로 옮긴다
 */
SW_TEST_CASE( EditorInspectorCommandsTest, AdoptMovesChildInstancesWhenNothingReferencesThem )
{
    AdoptScene                                 fixture;
    const EditorInspectorCommands::AdoptResult result = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_FALSE( result.isRejected() );
    SW_EXPECT_EQUAL( uint32( 1 ), result._adoptedCount );
    SW_EXPECT_TRUE( fixture._pMain->getObjectManager()->getSubsystem<PrefabInstanceRegistry>().isAttachedNestedPart( fixture._pLamp ) );
    SW_EXPECT_TRUE( fixture._workspace.getPlacedPrefabPath( *fixture._pLamp ).empty() );
}

/**
 * @brief [EditorInspectorCommandsTest] 옮길 자식을 다른 씬의 `SceneObjectReference` 가 가리키면 거절하고 아무것도 옮기지 않는다
 * @details 옮기면 그 오브젝트는 씬이 저장하지 않고 인스턴스가 다시 스폰해 파일 id 가 다음 로드에 사라진다 — 참조가 `ObjectMissing` 이 된다. 거절 이유에 어느 참조인지 적힌다.
 */
SW_TEST_CASE( EditorInspectorCommandsTest, AdoptRefusesWhenASceneReferencePointsAtAChild )
{
    AdoptScene fixture;
    auto*      pProbe = fixture._pHolder->addComponent<editortest::SceneReferenceHolderProbeComponent>();
    SW_ASSERT_NOT_NULL( pProbe );
    SW_ASSERT_TRUE( SceneObjectReferenceUtil::makeReference( *fixture._pMain, fixture._pLamp->getObjectID(), pProbe->_target ) );

    const EditorInspectorCommands::AdoptResult result = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_TRUE( result.isRejected() );
    SW_EXPECT_EQUAL( uint32( 0 ), result._adoptedCount );
    SW_EXPECT_TRUE( result._rejectReason.find( "Level/Holder." ) != string::npos );
    SW_EXPECT_TRUE( result._rejectReason.find( "._target" ) != string::npos );
    SW_EXPECT_TRUE( result._rejectReason.find( "Lamp" ) != string::npos );
    // 아무것도 옮기지 않았다 — 인스턴스 연결이 그대로다.
    SW_EXPECT_FALSE( fixture._pMain->getObjectManager()->getSubsystem<PrefabInstanceRegistry>().isAttachedNestedPart( fixture._pLamp ) );
    SW_EXPECT_TRUE( fixture._workspace.getPlacedPrefabPath( *fixture._pLamp ) == "game/prefabs/lamp.prefab.xml" );

    // 참조를 지우면 옮겨진다.
    pProbe->_target                                  = SceneObjectReference{};
    const EditorInspectorCommands::AdoptResult retry = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_FALSE( retry.isRejected() );
    SW_EXPECT_EQUAL( uint32( 1 ), retry._adoptedCount );
}

/**
 * @brief [EditorInspectorCommandsTest] `vector<SceneObjectReference>` 칸의 원소가 옮길 자식을 가리켜도 거절하고, 원소를 지우면 옮긴다
 */
SW_TEST_CASE( EditorInspectorCommandsTest, AdoptRefusesWhenAReferenceListElementPointsAtAChild )
{
    AdoptScene fixture;
    auto*      pProbe = fixture._pHolder->addComponent<editortest::SceneReferenceListHolderProbeComponent>();
    SW_ASSERT_NOT_NULL( pProbe );
    pProbe->_listTarget.resize( 2 ); // 앞 원소는 비워 두고 뒤 원소만 가리킨다
    SW_ASSERT_TRUE( SceneObjectReferenceUtil::makeReference( *fixture._pMain, fixture._pLamp->getObjectID(), pProbe->_listTarget[1] ) );

    const EditorInspectorCommands::AdoptResult result = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_TRUE( result.isRejected() );
    SW_EXPECT_EQUAL( uint32( 0 ), result._adoptedCount );
    SW_EXPECT_TRUE( result._rejectReason.find( "Level/Holder." ) != string::npos );
    SW_EXPECT_TRUE( result._rejectReason.find( "._listTarget" ) != string::npos );
    SW_EXPECT_FALSE( fixture._pMain->getObjectManager()->getSubsystem<PrefabInstanceRegistry>().isAttachedNestedPart( fixture._pLamp ) );

    pProbe->_listTarget.clear();
    const EditorInspectorCommands::AdoptResult retry = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_FALSE( retry.isRejected() );
    SW_EXPECT_EQUAL( uint32( 1 ), retry._adoptedCount );
}

/**
 * @brief [EditorInspectorCommandsTest] 구조체 칸 안의 `SceneObjectReference` 가 옮길 자식을 가리켜도 거절하고, 지우면 옮긴다
 */
SW_TEST_CASE( EditorInspectorCommandsTest, AdoptRefusesWhenAStructFieldReferencePointsAtAChild )
{
    AdoptScene fixture;
    auto*      pProbe = fixture._pHolder->addComponent<editortest::SceneReferenceStructHolderProbeComponent>();
    SW_ASSERT_NOT_NULL( pProbe );
    SW_ASSERT_TRUE( SceneObjectReferenceUtil::makeReference( *fixture._pMain, fixture._pLamp->getObjectID(), pProbe->_box._inner ) );

    const EditorInspectorCommands::AdoptResult result = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_TRUE( result.isRejected() );
    SW_EXPECT_EQUAL( uint32( 0 ), result._adoptedCount );
    SW_EXPECT_TRUE( result._rejectReason.find( "._box" ) != string::npos );
    SW_EXPECT_FALSE( fixture._pMain->getObjectManager()->getSubsystem<PrefabInstanceRegistry>().isAttachedNestedPart( fixture._pLamp ) );

    pProbe->_box._inner                              = SceneObjectReference{};
    const EditorInspectorCommands::AdoptResult retry = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_FALSE( retry.isRejected() );
    SW_EXPECT_EQUAL( uint32( 1 ), retry._adoptedCount );
}

/**
 * @brief [EditorInspectorCommandsTest] 미리보기 씬의 컴포넌트가 옮길 자식을 가리켜도 거절한다
 * @details 훑을 씬 목록은 `SceneManager::collectAllScenes` 한 곳이라 로드된 씬만 보던 때는 이 참조를 놓쳤다.
 */
SW_TEST_CASE( EditorInspectorCommandsTest, AdoptRefusesWhenAPreviewSceneReferencePointsAtAChild )
{
    AdoptScene fixture;
    Scene*     pPreview = fixture._sceneManager.createPreviewScene( "Preview" );
    SW_ASSERT_NOT_NULL( pPreview );
    GameObject* pPreviewHolder = pPreview->getObjectManager()->createGameObject( hashed_string( "PreviewHolder" ) );
    pPreview->getObjectManager()->mergePendingAdds();
    auto* pProbe = pPreviewHolder->addComponent<editortest::SceneReferenceHolderProbeComponent>();
    SW_ASSERT_NOT_NULL( pProbe );
    SW_ASSERT_TRUE( SceneObjectReferenceUtil::makeReference( *fixture._pMain, fixture._pLamp->getObjectID(), pProbe->_target ) );

    const EditorInspectorCommands::AdoptResult result = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_TRUE( result.isRejected() );
    SW_EXPECT_EQUAL( uint32( 0 ), result._adoptedCount );
    SW_EXPECT_TRUE( result._rejectReason.find( "PreviewHolder." ) != string::npos );

    pProbe->_target                                  = SceneObjectReference{};
    const EditorInspectorCommands::AdoptResult retry = EditorInspectorCommands::adoptChildPrefabInstances( fixture._pRoot );
    SW_EXPECT_FALSE( retry.isRejected() );
    SW_EXPECT_EQUAL( uint32( 1 ), retry._adoptedCount );
}
