#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "EditorTest/EditorPreviewProbe.h"
#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
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
