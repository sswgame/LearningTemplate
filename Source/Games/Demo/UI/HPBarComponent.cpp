#include "pch.h"

#include "Games/Demo/UI/HPBarComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Data/UnitStatsComponent.h"

#include "RuntimeAPI/GameService.h"

namespace sw
{
	HPBarComponent::HPBarComponent()
		: targetKind{ HPBarTargetKind::Monster }
		, bossName{}
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

		switch ( targetKind )
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
				const hashed_string targetBossName( bossName.c_str() );
				for ( GameObject* pObj : bossList )
				{
					if ( bossName.empty() || ( pObj != nullptr && pObj->getName() == targetBossName ) )
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
		}

		if ( pTargetObj != nullptr )
		{
			UnitStatsComponent* pStats = pTargetObj->getComponent<UnitStatsComponent>().get();
			if ( pStats != nullptr )
			{
				const UnitStatsData* pStatsData = pStats->getStatsData();
				HPBarBaseData*		 pBarData	= ensureHPBarData();

				if ( pStatsData != nullptr && pBarData != nullptr )
				{
					if ( pStatsData->maxHp > 0 )
						pBarData->targetRatio = static_cast<float32>( pStatsData->hp ) / static_cast<float32>( pStatsData->maxHp );
					else
						pBarData->targetRatio = 0.0f;
				}
			}
		}
	}
} // namespace sw
