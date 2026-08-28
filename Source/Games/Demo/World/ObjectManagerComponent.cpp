#include "pch.h"

#include "Games/Demo/World/ObjectManagerComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	void ObjectManagerComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PostUpdate );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Manager"_tag );

		_bInitialized	   = true;
		_activeObjectCount = 0;
		Scene* pScene	   = game::getService<SceneManager>()->getActiveScene();
		if ( pScene != nullptr )
		{
			GameObjectManager* pGameObjectManager = pScene->getObjectManager();
			if ( pGameObjectManager != nullptr )
				_activeObjectCount = static_cast<int32>( pGameObjectManager->getAllGameObjects().size() );
		}
	}

	void ObjectManagerComponent::onEndPlay()
	{
	}

	void ObjectManagerComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;
		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene != nullptr )
		{
			GameObjectManager* pGameObjectManager = pScene->getObjectManager();
			if ( pGameObjectManager != nullptr )
				_activeObjectCount = static_cast<int32>( pGameObjectManager->getAllGameObjects().size() );
		}
	}
} // namespace sw
