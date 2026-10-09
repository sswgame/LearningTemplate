#include "pch.h"

#include "Editor/Viewport/EditorCamera.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct EditorCameraInternal
        {
            static constexpr const utf8* kEditorCameraObjectName = "EditorCamera";

            static CameraComponent* createEditorCamera( GameObjectManager* pObjectManager )
            {
                return CameraComponent::findOrCreateNamed( pObjectManager, hashed_string{ kEditorCameraObjectName }, CameraRole::Editor,
                                                           float3( 2.15f, 1.55f, 2.65f ), float3( 0.0f, 0.0f, 0.0f ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    CameraComponent* EditorCamera::find( const Scene* pScene )
    {
        if ( pScene == nullptr )
            return nullptr;
        GameObjectManager* pObjectManager = pScene->getObjectManager();
        if ( pObjectManager == nullptr )
            return nullptr;

        // 등록부의 규칙(역할 · 우선순위) 하나로 고른다. 프레임마다 세 번(뷰포트 update · draw, 게임 스레드의 뷰 카메라) 불리는 자리라
        // 씬 전체를 돌며 오브젝트마다 `getComponent<CameraComponent>()` 를 묻지 않는다.
        CameraComponent* pBest = pObjectManager->getCameraRegistry().selectCamera( CameraRole::Editor );
        if ( pBest != nullptr )
            return pBest;

        GameObject* pNamed = pObjectManager->findGameObjectByName( hashed_string( EditorCameraInternal::kEditorCameraObjectName ) );
        if ( pNamed == nullptr )
            return nullptr;
        CameraComponent* pNamedCam = pNamed->getComponent<CameraComponent>();
        if ( pNamedCam == nullptr || pNamedCam->getRole() != CameraRole::Editor )
            return nullptr;
        return pNamedCam;
    }

    CameraComponent* EditorCamera::ensure( Scene* pScene )
    {
        CameraComponent* pExisting = find( pScene );
        if ( pExisting != nullptr && pExisting->isPendingDestroy() == false )
            return pExisting;
        if ( pScene == nullptr )
            return nullptr;

        GameObjectManager* pObjectManager = pScene->getObjectManager();
        if ( pObjectManager == nullptr )
            return nullptr;

        pObjectManager->flushSceneTransforms();
        CameraComponent* pCreated = EditorCameraInternal::createEditorCamera( pObjectManager );
        pObjectManager->flushSceneTransforms();
        return pCreated;
    }

} // namespace sw::editor
