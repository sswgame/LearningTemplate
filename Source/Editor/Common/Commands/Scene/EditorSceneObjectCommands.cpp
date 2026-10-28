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
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

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
                    EditorSceneObjectCommands::removeComponent( pOwner, pComponent );
            }

            static bool hasTargetComponent( const EditorCommandTarget& target )
            {
                GameObject* pOwner = nullptr;
                return EditorSceneObjectCommands::findTargetComponent( target, pOwner ) != nullptr;
            }

            static EditorWorkspace& getWorkspace() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>(); }

            static void runCopyComponent( const EditorCommandTarget& target )
            {
                GameObject*      pOwner     = nullptr;
                const Component* pComponent = EditorSceneObjectCommands::findTargetComponent( target, pOwner );
                if ( pComponent != nullptr )
                    getWorkspace().copyComponent( pComponent );
            }

            /** @brief 복사한 컴포넌트가 대상과 같은 타입이면 true 입니다(값 붙여 넣기). */
            static bool canPasteComponentValues( const EditorCommandTarget& target )
            {
                GameObject*            pOwner     = nullptr;
                const Component*       pComponent = EditorSceneObjectCommands::findTargetComponent( target, pOwner );
                const EditorWorkspace& workspace  = getWorkspace();
                if ( pComponent == nullptr || workspace.hasCopiedComponent() == false )
                    return false;
                return workspace.getCopiedComponentTypeName() == pComponent->getTypeName().c_str();
            }

            static void runPasteComponentValues( const EditorCommandTarget& target )
            {
                GameObject* pOwner     = nullptr;
                Component*  pComponent = EditorSceneObjectCommands::findTargetComponent( target, pOwner );
                if ( pComponent != nullptr )
                    (void)EditorTransformCommands::pasteCopiedComponentValues( getWorkspace(), pComponent ); // 실패는 붙여넣기가 로그로 알린다
            }

            static bool canPasteComponentAsNew( const EditorCommandTarget& target ) { return hasTargetComponent( target ) && getWorkspace().hasCopiedComponent(); }

            static void runPasteComponentAsNew( const EditorCommandTarget& target )
            {
                GameObject* pOwner = nullptr;
                if ( EditorSceneObjectCommands::findTargetComponent( target, pOwner ) != nullptr )
                    (void)EditorTransformCommands::pasteCopiedComponentAsNew( getWorkspace(), pOwner ); // 실패는 붙여넣기가 로그로 알린다
            }

            /** @brief 컴포넌트를 @p direction(-1 위 · +1 아래) 칸 옮길 수 있으면 true 입니다. 주 씬 컴포넌트(루트 트랜스폼)는 옮기지 않고 그 자리로 옮겨 들어가지도 않는다. */
            static bool canMoveComponent( const EditorCommandTarget& target, int32 direction )
            {
                GameObject*      pOwner     = nullptr;
                const Component* pComponent = EditorSceneObjectCommands::findTargetComponent( target, pOwner );
                if ( pComponent == nullptr )
                    return false;
                const auto& listComponent = pOwner->getComponents();
                size_t      index         = 0;
                while ( index < listComponent.size() && listComponent[index] != pComponent )
                {
                    ++index;
                }
                const Component* pPrimary  = pOwner->getPrimarySceneComponent();
                const bool       bMovable  = index < listComponent.size() && pComponent != pPrimary;
                const bool       bHasOther = direction < 0 ? index > 0 : index + 1 < listComponent.size();
                if ( ( bMovable && bHasOther ) == false )
                    return false;
                return listComponent[direction < 0 ? index - 1 : index + 1] != pPrimary;
            }

            static void runMoveComponent( const EditorCommandTarget& target, int32 direction )
            {
                GameObject* pOwner     = nullptr;
                Component*  pComponent = EditorSceneObjectCommands::findTargetComponent( target, pOwner );
                if ( pComponent == nullptr )
                    return;
                // 인스펙터 편집과 같은 규칙(`InspectorObjectEdit`) — 정지 상태면 되돌리기 기록과 dirty, 플레이 중이면 사본을 바로 바꾼다.
                const bool           bRecord        = EditorPlaySession::areSceneEditsAllowed();
                const ObjectSnapshot beforeSnapshot = bRecord ? EditorTransaction::captureSnapshot( pOwner ) : ObjectSnapshot{};
                (void)pOwner->moveComponent( pComponent, direction ); // 끝 · 주 씬 컴포넌트면 그대로 — 켜짐 조건이 이미 막는다
                if ( bRecord )
                    EditorSceneCommands::commitModify( pOwner, beforeSnapshot, direction < 0 ? "Move Component Up" : "Move Component Down" );
            }

            static bool canMoveComponentUp( const EditorCommandTarget& target ) { return canMoveComponent( target, -1 ); }
            static bool canMoveComponentDown( const EditorCommandTarget& target ) { return canMoveComponent( target, 1 ); }
            static void runMoveComponentUp( const EditorCommandTarget& target ) { runMoveComponent( target, -1 ); }
            static void runMoveComponentDown( const EditorCommandTarget& target ) { runMoveComponent( target, 1 ); }
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
        pOutOwner = nullptr;
        if ( target._hoveredID == 0 )
            return nullptr;
        // 컴포넌트 id 만 든다(포인터를 프레임 넘어 들지 않는다) — 메뉴를 고르는 순간 주인을 찾는다.
        // 메인 씬 다음에 인스펙터가 겨눈 씬(미리보기 창)을 본다 — 미리보기 카드도 같은 컴포넌트 메뉴다.
        const Scene*             pFocusedScene = editor::getFocusedScene();
        GameObjectManager* const arrManager[]  = { editor::getActiveObjectManager(), pFocusedScene != nullptr ? pFocusedScene->getObjectManager() : nullptr };
        Component*               pFound        = nullptr;
        for ( GameObjectManager* pManager : arrManager )
        {
            if ( pManager == nullptr || pFound != nullptr )
                continue;
            pManager->forEachGameObject( [&]( GameObject* pObject )
            {
                if ( pFound != nullptr )
                    return;
                pFound = pObject->findComponentByID( target._hoveredID );
                if ( pFound != nullptr )
                    pOutOwner = pObject;
            } );
        }
        return pFound;
    }

    void EditorSceneObjectCommands::removeComponent( GameObject* pOwner, Component* pComponent )
    {
        GameObjectManager* pManager = pOwner->getManager();
        if ( pManager == nullptr )
            (void)pOwner->removeComponent( pComponent ); // 실패는 removeComponent 가 알리고 카드가 남는다
        else if ( EditorPlaySession::areSceneEditsAllowed() )
            (void)EditorSceneCommands::destroyComponent( pManager, pOwner, pComponent ); // 기록과 dirty 를 남긴다. 지울 수 없으면 그대로다
        else
            pManager->destroyComponent( pComponent ); // 플레이 사본이므로 Stop 이 되돌린다
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

    // 컴포넌트 메뉴 — 계층 창의 컴포넌트 행 · 인스펙터의 메인 카드 · 미리보기 카드가 같은 메뉴다. 프리셋 하위 메뉴(400)는 인스펙터 파일에 있다.
    // 지우기 · 옮기기는 플레이 중에도 된다(사본을 바로 바꾸고 Stop 이 되돌린다 — 인스펙터 편집 규칙). 그래서 편집 칸이 NoScene 이다.
    SW_EDITOR_CONTEXT_COMMAND( ComponentSelectOwner, "scene.component.selectOwner", "Select Owner GameObject", editoricon::kGameObject, "Scene", Component, 100, nullptr,
                               &EditorSceneObjectCommandsInternal::runSelectOwner, &EditorSceneObjectCommandsInternal::hasTargetComponent, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ComponentCopy, "scene.component.copy", "Copy Component", editoricon::kCopy, "Scene", Component, 200, nullptr,
                               &EditorSceneObjectCommandsInternal::runCopyComponent, &EditorSceneObjectCommandsInternal::hasTargetComponent, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ComponentPasteValues, "scene.component.pasteValues", "Paste Component Values", editoricon::kPlus, "Scene", Component, 210, nullptr,
                               &EditorSceneObjectCommandsInternal::runPasteComponentValues, &EditorSceneObjectCommandsInternal::canPasteComponentValues, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ComponentPasteAsNew, "scene.component.pasteAsNew", "Paste as New Component", editoricon::kPlus, "Scene", Component, 220, nullptr,
                               &EditorSceneObjectCommandsInternal::runPasteComponentAsNew, &EditorSceneObjectCommandsInternal::canPasteComponentAsNew, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ComponentMoveUp, "scene.component.moveUp", "Move Up", nullptr, "Scene", Component, 300, nullptr, &EditorSceneObjectCommandsInternal::runMoveComponentUp,
                               &EditorSceneObjectCommandsInternal::canMoveComponentUp, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ComponentMoveDown, "scene.component.moveDown", "Move Down", nullptr, "Scene", Component, 310, nullptr, &EditorSceneObjectCommandsInternal::runMoveComponentDown,
                               &EditorSceneObjectCommandsInternal::canMoveComponentDown, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ComponentRemove, "scene.component.remove", "Remove Component", editoricon::kTrash, "Scene", Component, 900, nullptr, &EditorSceneObjectCommandsInternal::runRemoveComponent,
                               &EditorSceneObjectCommandsInternal::hasTargetComponent, NoScene, nullptr );
} // namespace sw::editor
