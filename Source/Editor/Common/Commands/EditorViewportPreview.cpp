#include "pch.h"

#include "Editor/Common/Commands/EditorViewportPreview.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"
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

			static bool matchesAnimationGraphPath( const SpriteAnimatorComponent* pAnimator, string_view graphPath )
			{
				if ( pAnimator == nullptr )
					return false;
				if ( graphPath.empty() )
					return true;
				const string& animatorPath = pAnimator->getAnimationGraphPath();
				if ( animatorPath.empty() )
					return true;
				return FileUtil::pathsEqualNormalized( animatorPath, graphPath );
			}

			static bool isDialogueRunnerType( const TypeInfo* pType )
			{
				if ( pType == nullptr )
					return false;
				if ( pType->_name == hashed_string( "DialogueRunnerComponent" ) )
					return true;
				return pType->_fullyQualifiedName == hashed_string( "sw::DialogueRunnerComponent" );
			}

			static void invokeDialoguePreviewLine( Component* pComp, string_view speaker, string_view text )
			{
				if ( pComp == nullptr )
					return;
				const TypeInfo* pType = pComp->getTypeInfo();
				if ( pType == nullptr )
					return;
				TypeRegistry* pRegistry = editor::getService<TypeRegistry>();
				if ( pRegistry == nullptr )
					return;
				TaskArgs args;
				args.add( string{ speaker } );
				args.add( string{ text } );
				pRegistry->invokeMethod( pComp, pType->_fullyQualifiedName, hashed_string( "previewLine" ), args );
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "EditorViewportPreview" );

	void EditorViewportPreview::applyAnimationNode( string_view nodeName, string_view graphPath )
	{
		if ( nodeName.empty() )
			return;

		const string nodeNameStr{ nodeName };
		GameObject*	 pPrimary = EditorViewportPreviewInternal::getPrimaryObject();
		if ( pPrimary != nullptr )
		{
			SpriteAnimatorComponent* pPrimaryAnimator = pPrimary->getComponent<SpriteAnimatorComponent>();
			if ( pPrimaryAnimator != nullptr )
				pPrimaryAnimator->play( nodeNameStr, false );
		}

		GameObjectManager* pManager = EditorViewportPreviewInternal::getActiveObjectManager();
		if ( pManager == nullptr )
			return;
		const vector<GameObject*> listObject = pManager->getAllGameObjects();
		for ( GameObject* pObject : listObject )
		{
			if ( pObject == nullptr || pObject == pPrimary )
				continue;
			SpriteAnimatorComponent* pAnimator = pObject->getComponent<SpriteAnimatorComponent>();
			if ( EditorViewportPreviewInternal::matchesAnimationGraphPath( pAnimator, graphPath ) == false )
				continue;
			pAnimator->play( nodeNameStr, false );
		}
	}

	void EditorViewportPreview::applySequenceFrame( const SequenceAsset& asset, int32 frame )
	{
		SequenceTimelineUtil::applyFrame( EditorViewportPreviewInternal::getActiveObjectManager(), asset, frame );
	}

	void EditorViewportPreview::applyDialogueLine( string_view speaker, string_view text )
	{
		GameObjectManager* pManager = EditorViewportPreviewInternal::getActiveObjectManager();
		if ( pManager != nullptr )
		{
			if ( speaker.empty() == false )
			{
				GameObject* pSpeaker = pManager->findGameObjectByName( hashed_string{ speaker } );
				if ( pSpeaker != nullptr )
				{
					EditorContext* pContext = EditorContext::get();
					if ( pContext != nullptr )
						pContext->getWorkspace().selectGameObject( GameObjectPtr{ pSpeaker } );
				}
			}

			const vector<GameObject*> listObject = pManager->getAllGameObjects();
			for ( GameObject* pObject : listObject )
			{
				if ( pObject == nullptr )
					continue;
				const vector<Component*> listComp = pObject->getAllComponents();
				for ( Component* pComp : listComp )
				{
					if ( pComp == nullptr )
						continue;
					if ( EditorViewportPreviewInternal::isDialogueRunnerType( pComp->getTypeInfo() ) == false )
						continue;
					EditorViewportPreviewInternal::invokeDialoguePreviewLine( pComp, speaker, text );
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
