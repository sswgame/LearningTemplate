#include "pch.h"

#include "Editor/Common/Commands/EditorViewportPreview.h"

#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	namespace
	{
		struct EditorViewportPreviewInternal
		{
			static GameObject* getPrimaryObject()
			{
				EditorContext* pContext = EditorContext::get();
				if ( pContext == nullptr )
					return nullptr;
				return pContext->getWorkspace().getSelectedObject().get();
			}

			static GameObjectManager* getActiveObjectManager()
			{
				SceneManager* pSceneManager = editor::getService<SceneManager>();
				if ( pSceneManager == nullptr )
					return nullptr;
				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene == nullptr )
					return nullptr;
				return pScene->getObjectManager();
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "EditorViewportPreview" );

	void EditorViewportPreview::applyAnimationNode( string_view nodeName )
	{
		if ( nodeName.empty() )
			return;
		GameObject* pPrimary = EditorViewportPreviewInternal::getPrimaryObject();
		if ( pPrimary == nullptr )
			return;
		SpriteAnimatorComponent* pAnimator = pPrimary->getComponent<SpriteAnimatorComponent>();
		if ( pAnimator == nullptr )
			return;
		pAnimator->play( string{ nodeName }, false );
	}

	void EditorViewportPreview::applySequenceFrame( const SequenceAsset& asset, int32 frame )
	{
		GameObjectManager* pManager = EditorViewportPreviewInternal::getActiveObjectManager();
		if ( pManager == nullptr )
			return;

		SequencePlayer player;
		player.setAsset( asset );
		player.seekToFrame( frame );

		vector<const SequenceTrackItem*> listActive;
		player.collectActiveItems( listActive );
		for ( const SequenceTrackItem* pItem : listActive )
		{
			if ( pItem == nullptr || pItem->_targetObject.empty() )
				continue;
			GameObject* pTarget = pManager->findGameObjectByName( hashed_string( pItem->_targetObject.c_str() ) );
			if ( pTarget == nullptr )
				continue;
			if ( pItem->_type == 0 )
				pTarget->setActive( true );
		}
	}

	void EditorViewportPreview::applyDialogueLine( string_view speaker, string_view text )
	{
		if ( speaker.empty() == false )
		{
			GameObjectManager* pManager = EditorViewportPreviewInternal::getActiveObjectManager();
			if ( pManager != nullptr )
			{
				GameObject* pSpeaker = pManager->findGameObjectByName( hashed_string( string{ speaker }.c_str() ) );
				if ( pSpeaker != nullptr )
				{
					EditorContext* pContext = EditorContext::get();
					if ( pContext != nullptr )
						pContext->getWorkspace().selectGameObject( GameObjectPtr{ pSpeaker } );
				}
			}
		}
		if ( speaker.empty() == false || text.empty() == false )
			SW_LOG_INFO( "Dialogue preview [%#]: %#", string{ speaker }.c_str(), string{ text }.c_str() );
	}

	void EditorViewportPreview::applyMaterial( Material* pMaterial, string_view assetPath )
	{
		if ( assetPath.empty() == false )
		{
			ResourceManager* pResources = editor::getService<ResourceManager>();
			if ( pResources != nullptr )
			{
				Material* pCached = pResources->getMaterialManager().acquire( assetPath, nullptr );
				if ( pCached != nullptr && pMaterial != nullptr )
				{
					pCached->loadFromXml( pMaterial->saveToString() );
					pCached->rebuildPackedBuffer();
					pMaterial = pCached;
				}
			}
		}

		GameObject* pPrimary = EditorViewportPreviewInternal::getPrimaryObject();
		if ( pPrimary == nullptr )
			return;

		MeshComponent* pMesh = pPrimary->getComponent<MeshComponent>();
		if ( pMesh != nullptr && pMaterial != nullptr )
			pMesh->setMaterial( pMaterial );

		SpriteComponent* pSprite = pPrimary->getComponent<SpriteComponent>();
		if ( pSprite != nullptr && assetPath.empty() == false )
			pSprite->setMaterialName( string{ assetPath } );
	}
} // namespace sw::editor
