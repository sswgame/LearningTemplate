#include "pch.h"

#include "Games/Demo/UI/PlayerInfoUIComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Data/UnitStatsComponent.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	PlayerInfoUIComponent::PlayerInfoUIComponent()
		: playerLevel{ 0 }
		, playerHp{ 0 }
		, playerMaxHp{ 0 }
		, playerMp{ 0 }
	{
	}

	void PlayerInfoUIComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PostUpdate );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "UI"_tag );
	}

	void PlayerInfoUIComponent::onEndPlay()
	{
	}

	void PlayerInfoUIComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;

		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		GameObject* pPlayerObj = pGameObjectManager->findGameObjectByTag( "Player"_tag );
		if ( pPlayerObj == nullptr )
			return;

		UnitStatsComponent* pUnitStats = pPlayerObj->getComponent<UnitStatsComponent>().get();
		if ( pUnitStats == nullptr )
			return;

		const UnitStatsData* pData = pUnitStats->getStatsData();
		if ( pData != nullptr )
		{
			playerHp	= pData->hp;
			playerMaxHp = pData->maxHp;
		}
	}

	void PlayerInfoUIComponent::updateStats( int32 hp, int32 maxHp )
	{
		playerHp	= hp;
		playerMaxHp = maxHp;
	}
} // namespace sw
