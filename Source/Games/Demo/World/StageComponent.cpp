#include "pch.h"

#include "Games/Demo/World/StageComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Data/UnitStatsComponent.h"

#include "RuntimeAPI/GameService.h"

namespace sw
{
	StageComponent::StageComponent()
		: totalMonsters{ 0 }
		, remainingMonsters{ 0 }
		, bStageCleared{ false }
	{
	}

	void StageComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PostUpdate );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Stage"_tag );

		int32  count{ 0 };
		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene != nullptr )
		{
			GameObjectManager* pGameObjectManager = pScene->getObjectManager();
			if ( pGameObjectManager != nullptr )
			{
				vector<GameObject*> monsterList;
				pGameObjectManager->findGameObjectsByTag( "Monster"_tag, monsterList );
				count = static_cast<int32>( monsterList.size() );
			}
		}

		totalMonsters	  = count;
		remainingMonsters = count;
		bStageCleared	  = ( count == 0 );
	}

	void StageComponent::onEndPlay()
	{
	}

	void StageComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;

		int32  aliveMonsters{ 0 };
		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene != nullptr )
		{
			GameObjectManager* pGameObjectManager = pScene->getObjectManager();
			if ( pGameObjectManager != nullptr )
			{
				vector<GameObject*> monsterList;
				pGameObjectManager->findGameObjectsByTag( "Monster"_tag, monsterList );
				for ( GameObject* pObj : monsterList )
				{
					if ( pObj != nullptr )
					{
						UnitStatsComponent* pStats = pObj->getComponent<UnitStatsComponent>().get();
						if ( pStats != nullptr )
						{
							const UnitStatsData* pData = pStats->getStatsData();
							if ( pData != nullptr && pData->bIsDead == false && pData->hp > 0 )
								aliveMonsters++;
						}
						else
						{
							aliveMonsters++;
						}
					}
				}
			}
		}

		remainingMonsters = aliveMonsters;
		if ( remainingMonsters <= 0 && bStageCleared == false )
			bStageCleared = true;
	}

	void StageComponent::monsterKilled()
	{
		if ( remainingMonsters > 0 )
		{
			remainingMonsters--;
			if ( remainingMonsters <= 0 )
				bStageCleared = true;
		}
	}
} // namespace sw
