/**
 * @file EditorSceneObjectCommands.cpp
 * @brief 씬 오브젝트 · 컴포넌트 대상 명령의 등록 줄과 동작입니다. 계층 창 · 씬 뷰 맥락 메뉴(R4 · R5)와 `editor.input.xml` 의 단축키가 같은 명령을 부릅니다.
 * @details 메뉴의 대상은 선택 전체다(R3). 대상이 없으면(메뉴바 · 단축키) 메인 씬의 지금 선택이다. 한 오브젝트에만 뜻이 있는 명령(자식 만들기 · 형제 옮기기)은
 *          누른 항목(없으면 주 선택)에 작동한다. ImGui 를 쓰지 않는다 — 하위 메뉴(Add Component · Select All With)는 그리는 패널 파일에 있다.
 */
#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSceneObjectCommands.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSiblingOrder.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSceneObjectCommandsInternal
        {
            /** @brief 계층 창이 보이지 않는 에디터 카메라 오브젝트입니다(형제 번호를 다시 매길 때 뺀다). 없으면 nullptr 입니다. */
            static const GameObject* findEditorCameraObject()
            {
                const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
                return pEditorCamera != nullptr ? pEditorCamera->getOwner() : nullptr;
            }

            static void runCreate( const EditorCommandTarget& /*target*/ )
            {
                (void)EditorSceneCommands::create( editor::getActiveObjectManager(), nullptr ); // 만든 것을 선택한다 — 실패는 편집이 막힌 것이다
            }

            static void runCreateChild( const EditorCommandTarget& target )
            {
                (void)EditorSceneCommands::create( editor::getActiveObjectManager(), EditorSceneObjectCommands::findTargetObject( target ) ); // 위와 같다
            }

            static void runCreateFolder( const EditorCommandTarget& /*target*/ )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager != nullptr )
                    (void)EditorSiblingOrder::createFolder( *pManager, nullptr ); // 편집이 막혔으면 nullptr 이고 아무것도 바뀌지 않는다
            }

            static void runCreateFolderInside( const EditorCommandTarget& target )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager != nullptr )
                    (void)EditorSiblingOrder::createFolder( *pManager, EditorSceneObjectCommands::findTargetObject( target ) ); // 위와 같다
            }

            static void runMoveStep( const EditorCommandTarget& target, bool bDown )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = EditorSceneObjectCommands::findTargetObject( target );
                if ( pManager != nullptr && pObject != nullptr )
                    (void)EditorSiblingOrder::moveStep( *pManager, pObject, bDown, findEditorCameraObject() ); // 끝이면 아무것도 하지 않는다
            }

            static void runMoveUp( const EditorCommandTarget& target ) { runMoveStep( target, false ); }
            static void runMoveDown( const EditorCommandTarget& target ) { runMoveStep( target, true ); }

            static void runDuplicate( const EditorCommandTarget& target )
            {
                vector<GameObject*> listObject;
                EditorSceneObjectCommands::collectTargetObjects( target, listObject );
                vector<GameObject*> listCreated;
                EditorSceneCommands::duplicateObjects( editor::getActiveObjectManager(), listObject, listCreated ); // 되돌리기 한 단계
                if ( listCreated.empty() == false )
                    editor::getMainSceneSelection().selectObjects( listCreated, SelectionMode::Replace );
            }

            static void runCopy( const EditorCommandTarget& target )
            {
                vector<GameObject*> listObject;
                EditorSceneObjectCommands::collectTargetObjects( target, listObject );
                (void)EditorSceneCommands::copyObjects( listObject ); // 복사한 수는 붙여 넣을 때 확인할 수 있다
            }

            static void runPaste( const EditorCommandTarget& /*target*/ )
            {
                vector<GameObject*> listPasted;
                EditorSceneCommands::pasteObjects( editor::getActiveObjectManager(), listPasted ); // 되돌리기 한 단계이고, 붙여 넣은 것을 선택한다
            }

            static bool canPaste( const EditorCommandTarget& /*target*/ ) { return EditorSceneCommands::hasCopiedObjects(); }

            static void runUnparent( const EditorCommandTarget& target )
            {
                vector<GameObject*> listObject;
                EditorSceneObjectCommands::collectTargetObjects( target, listObject );
                for ( GameObject* pObject : listObject )
                {
                    if ( pObject->getParent() != nullptr )
                        (void)EditorSceneCommands::unparent( pObject ); // 부모가 없으면 아무것도 하지 않는다
                }
            }

            static bool hasParentedTarget( const EditorCommandTarget& target )
            {
                vector<GameObject*> listObject;
                EditorSceneObjectCommands::collectTargetObjects( target, listObject );
                for ( const GameObject* pObject : listObject )
                {
                    if ( pObject->getParent() != nullptr )
                        return true;
                }
                return false;
            }

            static void runDestroy( const EditorCommandTarget& target )
            {
                vector<GameObject*> listObject;
                EditorSceneObjectCommands::collectTargetObjects( target, listObject );
                (void)EditorSceneCommands::destroyObjects( editor::getActiveObjectManager(), listObject ); // 되돌리기 한 단계
                editor::getMainSceneSelection().clearObjectSelection();
            }

            static bool hasSingleTargetObject( const EditorCommandTarget& target ) { return EditorSceneObjectCommands::findTargetObject( target ) != nullptr; }

            static void runCreateHere( const EditorCommandTarget& target )
            {
                GameObject* pCreated = EditorSceneCommands::create( editor::getActiveObjectManager(), nullptr );
                if ( pCreated != nullptr && target._bHasWorldPosition )
                    EditorSceneCommands::applyLocalTransform( pCreated, target._worldPosition, float3{}, float3{ 1.0f, 1.0f, 1.0f } );
            }

            static void runSelectOwner( const EditorCommandTarget& target )
            {
                GameObject* pOwner = nullptr;
                if ( EditorSceneObjectCommands::findTargetComponent( target, pOwner ) != nullptr )
                    editor::getMainSceneSelection().selectObject( pOwner, SelectionMode::Replace );
            }

            static void runRemoveComponent( const EditorCommandTarget& target )
            {
                GameObject* pOwner     = nullptr;
                Component*  pComponent = EditorSceneObjectCommands::findTargetComponent( target, pOwner );
                if ( pComponent != nullptr )
                    (void)EditorSceneCommands::destroyComponent( editor::getActiveObjectManager(), pOwner, pComponent ); // 지울 수 없는 컴포넌트면 아무것도 바뀌지 않는다
            }

            static bool hasTargetComponent( const EditorCommandTarget& target )
            {
                GameObject* pOwner = nullptr;
                return EditorSceneObjectCommands::findTargetComponent( target, pOwner ) != nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorSceneObjectCommands::collectTargetObjects( const EditorCommandTarget& target, vector<GameObject*>& outListObject )
    {
        outListObject.clear();
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return;
        if ( target._listSelectedID.empty() )
        {
            editor::getMainSceneSelection().getSelectedObjects( outListObject );
            return;
        }
        for ( const uint64 objectID : target._listSelectedID )
        {
            GameObject* pObject = pManager->findGameObjectByID( objectID );
            if ( pObject != nullptr )
                outListObject.push_back( pObject );
        }
    }

    GameObject* EditorSceneObjectCommands::findTargetObject( const EditorCommandTarget& target )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return nullptr;
        if ( target._hoveredID != 0 )
            return pManager->findGameObjectByID( target._hoveredID );
        return editor::getMainSceneSelection().getPrimaryObject();
    }

    Component* EditorSceneObjectCommands::findTargetComponent( const EditorCommandTarget& target, GameObject*& pOutOwner )
    {
        pOutOwner                   = nullptr;
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr || target._hoveredID == 0 )
            return nullptr;
        // 컴포넌트 id 만 든다(포인터를 프레임 넘어 들지 않는다) — 메뉴를 고르는 순간 주인을 찾는다.
        Component* pFound = nullptr;
        pManager->forEachGameObject( [&]( GameObject* pObject )
        {
            if ( pFound != nullptr )
                return;
            pFound = pObject->findComponentByID( target._hoveredID );
            if ( pFound != nullptr )
                pOutOwner = pObject;
        } );
        return pFound;
    }

    bool EditorSceneObjectCommands::hasTargetObjects( const EditorCommandTarget& target )
    {
        vector<GameObject*> listObject;
        collectTargetObjects( target, listObject );
        return listObject.empty() == false;
    }

    // 오브젝트 메뉴(계층 창의 행 · 빈 곳, 씬 뷰에서 오브젝트 위). 100 단위가 바뀌는 자리에 구분선이 들어간다.
    SW_EDITOR_CONTEXT_COMMAND( ObjectCreate, "scene.object.create", "Create GameObject", editoricon::kGameObject, "Scene", GameObject, 100, nullptr, &EditorSceneObjectCommandsInternal::runCreate, nullptr, Scene,
                               nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectCreateChild, "scene.object.createChild", "Create Child GameObject", editoricon::kGameObject, "Scene", GameObject, 110, nullptr,
                               &EditorSceneObjectCommandsInternal::runCreateChild, &EditorSceneObjectCommandsInternal::hasSingleTargetObject, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectCreateFolder, "scene.object.createFolder", "Create Folder", editoricon::kFolder, "Scene", GameObject, 120, nullptr, &EditorSceneObjectCommandsInternal::runCreateFolder,
                               nullptr, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectCreateFolderInside, "scene.object.createFolderInside", "Create Folder Inside", editoricon::kFolder, "Scene", GameObject, 130, nullptr,
                               &EditorSceneObjectCommandsInternal::runCreateFolderInside, &EditorSceneObjectCommandsInternal::hasSingleTargetObject, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectMoveUp, "scene.object.moveUp", "Move Up", editoricon::kGameObject, "Scene", GameObject, 200, nullptr, &EditorSceneObjectCommandsInternal::runMoveUp,
                               &EditorSceneObjectCommandsInternal::hasSingleTargetObject, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectMoveDown, "scene.object.moveDown", "Move Down", editoricon::kGameObject, "Scene", GameObject, 210, nullptr, &EditorSceneObjectCommandsInternal::runMoveDown,
                               &EditorSceneObjectCommandsInternal::hasSingleTargetObject, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectDuplicate, "scene.duplicate", "Duplicate", editoricon::kCopy, "Scene", GameObject, 300, "scene.duplicate", &EditorSceneObjectCommandsInternal::runDuplicate,
                               &EditorSceneObjectCommands::hasTargetObjects, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectCopy, "scene.copy", "Copy", editoricon::kCopy, "Scene", GameObject, 310, "scene.copy", &EditorSceneObjectCommandsInternal::runCopy, &EditorSceneObjectCommands::hasTargetObjects,
                               NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectPaste, "scene.paste", "Paste", editoricon::kPlus, "Scene", GameObject, 320, "scene.paste", &EditorSceneObjectCommandsInternal::runPaste, &EditorSceneObjectCommandsInternal::canPaste, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectUnparent, "scene.object.unparent", "Unparent", editoricon::kGameObject, "Scene", GameObject, 500, nullptr, &EditorSceneObjectCommandsInternal::runUnparent,
                               &EditorSceneObjectCommandsInternal::hasParentedTarget, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectDestroy, "scene.delete", "Delete", editoricon::kTrash, "Scene", GameObject, 900, "scene.delete", &EditorSceneObjectCommandsInternal::runDestroy,
                               &EditorSceneObjectCommands::hasTargetObjects, Scene, nullptr );

    // 씬 뷰의 빈 곳 메뉴(R6 — 코어 항목은 확장이 없어도 열린다).
    SW_EDITOR_CONTEXT_COMMAND( ViewportCreateHere, "scene.viewport.createHere", "Create Empty Here", editoricon::kGameObject, "Scene", Viewport, 100, nullptr, &EditorSceneObjectCommandsInternal::runCreateHere,
                               nullptr, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ViewportPaste, "scene.viewport.paste", "Paste", editoricon::kPlus, "Scene", Viewport, 110, nullptr, &EditorSceneObjectCommandsInternal::runPaste, &EditorSceneObjectCommandsInternal::canPaste, Scene,
                               nullptr );

    // 컴포넌트 메뉴(계층 창의 컴포넌트 행).
    SW_EDITOR_CONTEXT_COMMAND( ComponentSelectOwner, "scene.component.selectOwner", "Select Owner GameObject", editoricon::kGameObject, "Scene", Component, 100, nullptr,
                               &EditorSceneObjectCommandsInternal::runSelectOwner, &EditorSceneObjectCommandsInternal::hasTargetComponent, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ComponentRemove, "scene.component.remove", "Remove Component", editoricon::kTrash, "Scene", Component, 900, nullptr, &EditorSceneObjectCommandsInternal::runRemoveComponent,
                               &EditorSceneObjectCommandsInternal::hasTargetComponent, Scene, nullptr );
} // namespace sw::editor
