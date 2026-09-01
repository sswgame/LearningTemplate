#include "pch.h"

#include "Games/Demo/World/StageComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/GameService.h"
#include "GameFramework/Data/UnitStatsComponent.h"

namespace sw
{
	StageComponent::StageComponent()
		: _totalMonsters{ 0 }
		, _remainingMonsters{ 0 }
		, _bStageCleared{ false }
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
				vector<GameObject*> listMonster;
				pGameObjectManager->findGameObjectsByTag( "Monster"_tag, listMonster );
				count = static_cast<int32>( listMonster.size() );
			}
		}

		_totalMonsters	   = count;
		_remainingMonsters = count;
		_bStageCleared	   = ( count == 0 );
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
				vector<GameObject*> listMonster;
				pGameObjectManager->findGameObjectsByTag( "Monster"_tag, listMonster );
				for ( GameObject* pObj : listMonster )
				{
					if ( pObj != nullptr )
					{
						UnitStatsComponent* pStats = pObj->getComponent<UnitStatsComponent>();
						if ( pStats != nullptr )
						{
							if ( pStats->isDead() == false && pStats->getHp() > 0 )
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

		_remainingMonsters = aliveMonsters;
		if ( _remainingMonsters <= 0 && _bStageCleared == false )
			_bStageCleared = true;
	}

	void StageComponent::monsterKilled()
	{
		if ( _remainingMonsters > 0 )
		{
			_remainingMonsters--;
			if ( _remainingMonsters <= 0 )
				_bStageCleared = true;
		}
	}
} // namespace sw
