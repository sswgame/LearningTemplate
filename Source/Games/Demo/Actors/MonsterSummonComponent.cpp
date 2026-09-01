#include "pch.h"

#include "Games/Demo/Actors/MonsterSummonComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceManager.h"

#include "GameFramework/Base/GameService.h"

#include "Games/Demo/Actors/MonsterComponent.h"

namespace sw
{
	void MonsterSummonComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Monster"_tag );
	}

	void MonsterSummonComponent::onEndPlay()
	{
	}

	void MonsterSummonComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;
	}

	void MonsterSummonComponent::spawnMonsters()
	{
		if ( _summonCount <= 0 )
			return;

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pOwnerSceneComp = pOwner->getPrimarySceneComponent();
		if ( pOwnerSceneComp == nullptr )
			return;

		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		const float3  ownerWorldPos = pOwnerSceneComp->getWorldPosition();
		const int32	  count			= _summonCount;
		const float32 radius		= _summonRadius;
		const string  prefabOrId	= _monsterPrefab;

		auto spawnAll = [pGameObjectManager, ownerWorldPos, count, radius, prefabOrId]()
		{
			const float32 angleStep = ( MathUtil::Pi * 2.0f ) / static_cast<float32>( count );
			for ( int32 summonIndex = 0; summonIndex < count; ++summonIndex )
			{
				const float32 angle	   = static_cast<float32>( summonIndex ) * angleStep;
				const float3  spawnPos = ownerWorldPos + float3{ MathUtil::cos( angle ) * radius, MathUtil::sin( angle ) * radius, 0.0f };

				GameObject* pMinion = nullptr;
				if ( game::areGameServicesBound() && prefabOrId.find( '/' ) != string::npos )
				{
					StringBuilder<constant::kMaxBuffer64> nameBuilder;
					nameBuilder.append( "Minion_" ).append( summonIndex );
					pMinion = game::getService<ResourceManager>()->getPrefabManager().spawn(
						pGameObjectManager, prefabOrId, nameBuilder.c_str() );
				}

				if ( pMinion == nullptr )
				{
					StringBuilder<constant::kMaxBuffer64> nameBuilder;
					nameBuilder.append( prefabOrId.c_str(), static_cast<uint32>( prefabOrId.size() ) ).append( "_Minion_" ).append( summonIndex );
					pMinion = pGameObjectManager->createGameObject( hashed_string( nameBuilder.c_str(), nameBuilder.size() ) );
					if ( pMinion == nullptr )
						continue;

					SceneComponent* pSceneComp = pMinion->addComponent<SceneComponent>();
					if ( pSceneComp != nullptr )
						pSceneComp->setLocalPosition( spawnPos );

					MonsterComponent* pMonsterComp = pMinion->addComponent<MonsterComponent>();
					if ( pMonsterComp != nullptr )
						pMonsterComp->_monsterId = prefabOrId;
				}
				else
				{
					SceneComponent* pSceneComp = pMinion->getPrimarySceneComponent();
					if ( pSceneComp != nullptr )
						pSceneComp->setLocalPosition( spawnPos );

					MonsterComponent* pMonsterComp = pMinion->getComponent<MonsterComponent>();
					if ( pMonsterComp != nullptr && pMonsterComp->_monsterId.empty() )
						pMonsterComp->_monsterId = prefabOrId;
				}

				pMinion->addTag( "Monster"_tag );
			}
		};

		pGameObjectManager->executeOrDeferPostTick( spawnAll );
	}
} // namespace sw
