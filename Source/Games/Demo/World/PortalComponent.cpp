#include "pch.h"

#include "Games/Demo/World/PortalComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	void PortalComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Portal"_tag );
	}

	void PortalComponent::onEndPlay()
	{
	}

	void PortalComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;

		if ( bIsOpen == false || targetScenePath.empty() )
			return;

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pOwnerSceneComp = pOwner->getPrimarySceneComponent();
		if ( pOwnerSceneComp == nullptr )
			return;

		const float3 ownerWorldPos = pOwnerSceneComp->getWorldPosition();

		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		GameObject* pPlayerObj = pGameObjectManager->findGameObjectByTag( "Player"_tag );
		if ( pPlayerObj == nullptr )
			return;

		SceneComponent* pPlayerSceneComp = pPlayerObj->getPrimarySceneComponent();
		if ( pPlayerSceneComp != nullptr )
		{
			const float3  playerWorldPos  = pPlayerSceneComp->getWorldPosition();
			const float32 deltaX		  = playerWorldPos._x - ownerWorldPos._x;
			const float32 deltaY		  = playerWorldPos._y - ownerWorldPos._y;
			const float32 distSq		  = deltaX * deltaX + deltaY * deltaY;
			const float32 triggerRadiusSq = triggerRadius * triggerRadius;

			if ( triggerRadius > 0.0f && distSq <= triggerRadiusSq )
			{
				SceneManager& sceneManager = *game::getService<SceneManager>();
				sceneManager.requestLoadAsync( targetScenePath );
				bIsOpen = false;
			}
		}
	}
} // namespace sw
