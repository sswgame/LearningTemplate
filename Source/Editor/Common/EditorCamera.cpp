#include "pch.h"

#include "Editor/Common/EditorCamera.h"

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
		constexpr const utf8* kEditorCameraObjectName = "EditorCamera";

		CameraComponent* createEditorCamera( GameObjectManager* pObjectManager )
		{
			if ( pObjectManager == nullptr )
				return nullptr;

			const hashed_string cameraName{ kEditorCameraObjectName };
			GameObject*			pObj = pObjectManager->findGameObjectByName( cameraName );
			if ( pObj == nullptr )
				pObj = pObjectManager->createGameObject( cameraName );
			if ( pObj == nullptr )
				return nullptr;

			CameraComponent* pCam = pObj->getComponent<CameraComponent>();
			if ( pCam == nullptr )
				pCam = pObj->addComponent<CameraComponent>();
			if ( pCam == nullptr )
				return nullptr;

			pCam->setRole( CameraRole::Editor );
			pCam->setLocalPosition( float3( 2.15f, 1.55f, 2.65f ) );
			pCam->lookAt( float3( 0.0f, 0.0f, 0.0f ) );
			pCam->setFieldOfViewY( 0.70f );
			pCam->setNearPlane( 0.1f );
			pCam->setFarPlane( 100.0f );
			return pCam;
		}
	} // namespace

	CameraComponent* EditorCamera::find( const Scene* pScene )
	{
		if ( pScene == nullptr )
			return nullptr;
		GameObjectManager* pObjectManager = pScene->getObjectManager();
		if ( pObjectManager == nullptr )
			return nullptr;

		int32			 bestPriority = MathUtil::MinInt32;
		CameraComponent* pBest{ nullptr };
		for ( GameObject* pObj : pObjectManager->getAllGameObjects() )
		{
			if ( pObj == nullptr || pObj->isActive() == false )
				continue;
			CameraComponent* pCam = pObj->getComponent<CameraComponent>();
			if ( pCam == nullptr || pCam->isActive() == false || pCam->isPendingKill() == true )
				continue;
			if ( pCam->getRole() != CameraRole::Editor )
				continue;
			if ( pCam->getPriority() < bestPriority )
				continue;
			bestPriority = pCam->getPriority();
			pBest		 = pCam;
		}
		if ( pBest != nullptr )
			return pBest;

		GameObject* pNamed = pObjectManager->findGameObjectByName( hashed_string( kEditorCameraObjectName ) );
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
		if ( pExisting != nullptr && pExisting->isPendingKill() == false )
			return pExisting;
		if ( pScene == nullptr )
			return nullptr;

		GameObjectManager* pObjectManager = pScene->getObjectManager();
		if ( pObjectManager == nullptr )
			return nullptr;

		pObjectManager->flushSceneTransforms();
		CameraComponent* pCreated = createEditorCamera( pObjectManager );
		pObjectManager->flushSceneTransforms();
		return pCreated;
	}

	CameraComponent* EditorCamera::getViewportCamera( Scene* pScene, bool bPlaying )
	{
		if ( pScene == nullptr )
			return nullptr;
		if ( bPlaying )
		{
			pScene->ensureDefaultCameras();
			return pScene->getActiveGameCamera();
		}
		return ensure( pScene );
	}
} // namespace sw::editor
