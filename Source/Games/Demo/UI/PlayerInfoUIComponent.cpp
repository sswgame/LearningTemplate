#include "pch.h"

#include "Games/Demo/UI/PlayerInfoUIComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Data/UnitStatsComponent.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	PlayerInfoUIComponent::PlayerInfoUIComponent()
		: _playerLevel{ 0 }
		, _playerHp{ 0 }
		, _playerMaxHp{ 0 }
		, _playerMp{ 0 }
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

		UnitStatsComponent* pUnitStats = pPlayerObj->getComponent<UnitStatsComponent>();
		if ( pUnitStats == nullptr )
			return;

		_playerHp	 = pUnitStats->getHp();
		_playerMaxHp = pUnitStats->getMaxHp();
	}

	void PlayerInfoUIComponent::updateStats( int32 hp, int32 maxHp )
	{
		_playerHp	 = hp;
		_playerMaxHp = maxHp;
	}
} // namespace sw
