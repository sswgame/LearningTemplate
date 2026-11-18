#include "pch.h"

#include "Editor/Viewport/Model/EditorViewportPick.h"

#include "Core/Math/AABB.h"

#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/EditorSceneIterationAccess.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/Model/EditorCanvasProjection.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportPickInternal
        {
            static SelectionMode toSelectionMode( EditorPickSelectMode mode )
            {
                switch ( mode )
                {
                    case EditorPickSelectMode::Add:
                        return SelectionMode::Add;
                    case EditorPickSelectMode::Toggle:
                        return SelectionMode::Toggle;
                    case EditorPickSelectMode::Replace:
                        break;
                }
                return SelectionMode::Replace;
            }

            static EditorWorkspace& getWorkspace() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>(); }

            /** @brief 활성 씬의 오브젝트 매니저입니다. 씬이 없으면 nullptr 입니다. */
            static GameObjectManager* findManager()
            {
                Scene* pScene = editor::getActiveScene();
                return pScene != nullptr ? pScene->getObjectManager() : nullptr;
            }

            /** @brief 상자 선택 사각형이 오브젝트에 걸치는지 봅니다(`EditorViewportPick::selectInRect`). 피벗 한 점만 보면 큰 물체를 반쯤 덮어도 고르지 못한다(감사 E-10). */
            static bool isTouchedByRect( const GameObject& object, const SceneComponent& root, const EditorViewportPickInput& input, const float2& minPos, const float2& maxPos )
            {
                AABB box{};
                if ( object.getWorldBox( box ) == false )
                {
                    float2 screen{};
                    if ( EditorCanvasProjection::projectPoint( input._viewProj, root.getWorldPosition(), input._canvasPos, input._canvasSize, screen ) == false )
                        return false;
                    return minPos._x <= screen._x && screen._x <= maxPos._x && minPos._y <= screen._y && screen._y <= maxPos._y;
                }
                float2 screenMin{};
                float2 screenMax{};
                if ( EditorCanvasProjection::projectBoxRect( input._viewProj, box, input._canvasPos, input._canvasSize, screenMin, screenMax ) == false )
                    return false;
                return screenMin._x <= maxPos._x && minPos._x <= screenMax._x && screenMin._y <= maxPos._y && minPos._y <= screenMax._y;
            }
        };
    } // namespace

    uint32 EditorViewportPick::findBillboardAt( const vector<EditorVisualizerScreenBillboard>& listBillboard, const float2& point )
    {
        uint32  bestIndex    = constant::invalid_index::kUint32;
        float32 bestDistance = 0.0f;
        for ( size_t index = 0; index < listBillboard.size(); ++index )
        {
            const EditorVisualizerScreenBillboard& badge    = listBillboard[index];
            const float32                          distance = float2::getDistanceSquared( badge._screen, point );
            const bool                             bCloser  = distance <= badge._radius * badge._radius && ( bestIndex == constant::invalid_index::kUint32 || distance <= bestDistance );
            if ( bCloser == false )
                continue;
            bestDistance = distance;
            bestIndex    = static_cast<uint32>( index );
        }
        return bestIndex;
    }

    bool EditorViewportPick::findUnderMouse( const EditorViewportPickInput& input, const float2& mouse, const Ray* pRay, EditorViewportPickHit& outHit )
    {
        SW_EDITOR_PROFILE_SCOPE( "GT.Editor.viewportPick" );
        outHit                      = EditorViewportPickHit{};
        GameObjectManager* pManager = EditorViewportPickInternal::findManager();
        if ( pManager == nullptr || pRay == nullptr )
            return false;
        pManager->flushSceneTransforms();
        const EditorWorkspace& workspace = EditorViewportPickInternal::getWorkspace();

        if ( input._pListScreenBillboard != nullptr )
        {
            const uint32 index = findBillboardAt( *input._pListScreenBillboard, mouse );
            if ( index != constant::invalid_index::kUint32 )
            {
                const EditorVisualizerScreenBillboard& badge = ( *input._pListScreenBillboard )[index];
                if ( workspace.isObjectLocked( badge._objectID ) || pManager->findGameObjectByID( badge._objectID ) == nullptr )
                    return false;
                outHit._objectID    = badge._objectID;
                outHit._componentID = badge._componentID;
                return true;
            }
        }

        // 어떤 컴포넌트 종류를 집을 수 있는지는 EditorViewportRayPick 의 테이블이 정한다.
        EditorPickResult pickResult{};
        const bool       bNothingPicked = EditorViewportRayPick::pick( pManager, *pRay, input._b2DMode, pickResult ) == false || pickResult._pObject == nullptr ||
                                    workspace.isObjectLocked( pickResult._pObject->getObjectID() ) || pickResult._pObject->isRenderHidden();
        if ( bNothingPicked )
            return false;
        outHit._objectID    = pickResult._pObject->getObjectID();
        outHit._componentID = pickResult._pComponent != nullptr ? pickResult._pComponent->getComponentID() : 0;
        return true;
    }

    void EditorViewportPick::applyClick( const EditorViewportPickHit& hit, EditorPickSelectMode mode )
    {
        EditorWorkspace&   workspace = EditorViewportPickInternal::getWorkspace();
        GameObjectManager* pManager  = EditorViewportPickInternal::findManager();
        GameObject*        pObject   = ( pManager != nullptr && hit._objectID != 0 ) ? pManager->findGameObjectByID( hit._objectID ) : nullptr;
        if ( pObject == nullptr )
        {
            if ( mode == EditorPickSelectMode::Replace )
                workspace.clearSelection();
            return;
        }
        if ( mode == EditorPickSelectMode::Replace )
            workspace.selectComponent( pObject, hit._componentID != 0 ? pObject->findComponentByID( hit._componentID ) : nullptr );
        else
            workspace.selectGameObject( pObject, EditorViewportPickInternal::toSelectionMode( mode ) );
    }

    void EditorViewportPick::selectInRect( const EditorViewportPickInput& input, const float2& minPos, const float2& maxPos, EditorPickSelectMode mode )
    {
        GameObjectManager* pManager = EditorViewportPickInternal::findManager();
        if ( pManager == nullptr )
            return;
        pManager->flushSceneTransforms();
        const EditorWorkspace& workspace = EditorViewportPickInternal::getWorkspace();
        vector<GameObject*>    listInside;
        pManager->forEachGameObject( editor::EditorSceneIterationAccess::getKey(),
                                     [&]( GameObject* pObj )
        {
            const SceneComponent* pRoot    = pObj->getPrimarySceneComponent();
            const bool            bSkipped = pObj->getObjectID() == input._skipObjectID || pRoot == nullptr || pObj->isRenderHidden() || workspace.isObjectLocked( pObj->getObjectID() );
            if ( bSkipped )
                return;
            if ( EditorViewportPickInternal::isTouchedByRect( *pObj, *pRoot, input, minPos, maxPos ) )
                listInside.push_back( pObj );
        } );
        if ( listInside.empty() == false )
            editor::getMainSceneSelection().selectObjects( listInside, EditorViewportPickInternal::toSelectionMode( mode ) );
    }

    bool EditorViewportPick::isSelected( uint64 objectID )
    {
        GameObjectManager* pManager = EditorViewportPickInternal::findManager();
        GameObject*        pObject  = ( pManager != nullptr && objectID != 0 ) ? pManager->findGameObjectByID( objectID ) : nullptr;
        return pObject != nullptr && editor::getMainSceneSelection().hasObject( pObject );
    }

    void EditorViewportPick::getSelectedIDs( vector<uint64>& outListID )
    {
        outListID.clear();
        vector<GameObject*> listSelected;
        editor::getMainSceneSelection().getSelectedObjects( listSelected );
        for ( const GameObject* pSelected : listSelected )
        {
            outListID.push_back( pSelected->getObjectID() );
        }
    }

    void EditorViewportPick::selectOnly( uint64 objectID )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
        if ( pObject != nullptr )
            editor::getMainSceneSelection().selectObject( pObject, SelectionMode::Replace );
    }
} // namespace sw::editor
