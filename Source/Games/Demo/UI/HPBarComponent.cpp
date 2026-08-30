#include "pch.h"

#include "Games/Demo/UI/HPBarComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Data/UnitStatsComponent.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	HPBarComponent::HPBarComponent()
		: _targetKind{ HPBarTargetKind::Monster }
		, _bossName{}
	{
	}

	void HPBarComponent::onBeginPlay()
	{
		HPBarBaseComponent::onBeginPlay();
	}

	void HPBarComponent::onEndPlay()
	{
		HPBarBaseComponent::onEndPlay();
	}

	void HPBarComponent::onTick( float32 deltaTime )
	{
		HPBarBaseComponent::onTick( deltaTime );

		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		GameObject* pTargetObj{ nullptr };

		switch ( _targetKind )
		{
			case HPBarTargetKind::Player:
			{
				pTargetObj = pGameObjectManager->findGameObjectByTag( "Player"_tag );
				break;
			}
			case HPBarTargetKind::Boss:
			{
				vector<GameObject*> bossList;
				pGameObjectManager->findGameObjectsByTag( "Boss"_tag, bossList );
				const hashed_string targetBossName( _bossName.c_str() );
				for ( GameObject* pObj : bossList )
				{
					if ( _bossName.empty() || ( pObj != nullptr && pObj->getName() == targetBossName ) )
					{
						pTargetObj = pObj;
						break;
					}
				}
				break;
			}
			case HPBarTargetKind::Monster:
			{
				pTargetObj = getOwner();
				break;
			}
			default:
				break;
		}

		if ( pTargetObj != nullptr )
		{
			UnitStatsComponent* pStats = pTargetObj->getComponent<UnitStatsComponent>();
			if ( pStats != nullptr )
			{
				if ( pStats->getMaxHp() > 0 )
					setTargetRatio( static_cast<float32>( pStats->getHp() ) / static_cast<float32>( pStats->getMaxHp() ) );
				else
					setTargetRatio( 0.0f );
			}
		}
	}
} // namespace sw
