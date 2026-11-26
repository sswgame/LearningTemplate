#include "pch.h"

#include "Editor/Viewport/Model/EditorViewportClientScene.h"

#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPanelID.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelSubsystem.h"
#include "Editor/Viewport/EditorViewportCamera.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorGizmoEdit.h"
#include "Editor/Viewport/Model/EditorVisualizerGeometry.h"

#include "Engine/Graphics/DebugDraw/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportClientSceneInternal
        {
            /** @brief 씬 뷰 이미지를 그린 카메라(에디터 카메라)입니다. 활성 씬이 없으면 nullptr 입니다. */
            static CameraComponent* getSceneViewCamera() { return EditorCamera::ensure( editor::getActiveScene() ); }

            static EditorWorkspace& getWorkspace() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>(); }
        };
    } // namespace

    bool EditorViewportClientScene::gatherFrame( float32 aspect, EditorViewportFrame& outFrame )
    {
        CameraComponent* pCamera = EditorViewportClientSceneInternal::getSceneViewCamera();
        if ( pCamera == nullptr )
            return false;
        outFrame._view           = pCamera->getViewMatrix();
        outFrame._projection     = pCamera->getProjectionMatrix( aspect );
        outFrame._viewProj       = pCamera->getViewProjectionMatrix( aspect );
        outFrame._cameraObjectID = pCamera->getOwner() != nullptr ? pCamera->getOwner()->getObjectID() : 0;
        outFrame._bFlat2D        = EditorVisualizerGeometryUtil::isFlat2DView( pCamera->isOrthographic(), outFrame._view );
        return true;
    }

    void EditorViewportClientScene::applyCamera( EditorViewportCamera& camera )
    {
        camera.applyToCamera( EditorViewportClientSceneInternal::getSceneViewCamera() );
    }

    void EditorViewportClientScene::fillVisualizerScene( EditorViewportVisualizerArgs& inoutArgs )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        inoutArgs._pActiveCamera    = EditorViewportClientSceneInternal::getSceneViewCamera();
        inoutArgs._pListCamera      = pManager != nullptr ? &pManager->getCameraRegistry().getAll() : nullptr;
        inoutArgs._pListCollider    = pManager != nullptr ? &pManager->getSubsystem<sw::ScenePhysics>().getColliders2D() : nullptr;
        inoutArgs._pDebugDrawQueue  = &editor::getSubsystem<DebugDrawQueue>();
        inoutArgs._pObjectManager   = pManager;
    }

    void EditorViewportClientScene::flushSceneTransforms()
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager != nullptr )
            pManager->flushSceneTransforms();
    }

    void EditorViewportClientScene::gatherGizmo( EditorGizmoViewState& outState )
    {
        EditorGizmoEdit::gather( editor::getMainSceneSelection(), outState );
    }

    void EditorViewportClientScene::applyGizmo( EditorGizmoEdit& edit, const vector<EditorGizmoCommand>& listCommand )
    {
        edit.apply( editor::getMainSceneSelection(), listCommand );
    }

    void EditorViewportClientScene::focusMainSelection()
    {
        editor::setFocusedSelection( nullptr );
    }

    int32 EditorViewportClientScene::getGizmoOperation()
    {
        return EditorViewportClientSceneInternal::getWorkspace().getGizmoOperation();
    }

    void EditorViewportClientScene::setGizmoOperation( int32 operation )
    {
        EditorViewportClientSceneInternal::getWorkspace().setGizmoOperation( operation );
    }

    bool EditorViewportClientScene::maximizeSceneView()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<IEditorPanelSubsystem>().maximizePanel( editorpanel::kSceneView );
    }

    bool EditorViewportClientScene::restoreMaximizedLayout()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<IEditorPanelSubsystem>().restoreMaximizedLayout();
    }
} // namespace sw::editor
