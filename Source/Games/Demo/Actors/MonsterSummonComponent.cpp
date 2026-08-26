#include "pch.h"

#include "Games/Demo/Actors/MonsterSummonComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "Games/Demo/Actors/MonsterComponent.h"

#include "RuntimeAPI/Service/GameService.h"

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
		if ( summonCount <= 0 )
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
		const int32	  count			= summonCount;
		const float32 radius		= summonRadius;
		const string  prefabOrId	= monsterPrefab;

		auto spawnAll = [pGameObjectManager, ownerWorldPos, count, radius, prefabOrId]()
		{
			const float32 angleStep = 6.2831853f / static_cast<float32>( count );
			for ( int32 summonIndex = 0; summonIndex < count; ++summonIndex )
			{
				const float32 angle = static_cast<float32>( summonIndex ) * angleStep;
				const float3  spawnPos{
					ownerWorldPos._x + MathUtil::cos( angle ) * radius,
					ownerWorldPos._y + MathUtil::sin( angle ) * radius,
					ownerWorldPos._z };

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
						pMonsterComp->monsterId = prefabOrId;
				}
				else
				{
					SceneComponent* pSceneComp = pMinion->getPrimarySceneComponent();
					if ( pSceneComp != nullptr )
						pSceneComp->setLocalPosition( spawnPos );

					MonsterComponent* pMonsterComp = pMinion->getComponent<MonsterComponent>().get();
					if ( pMonsterComp != nullptr && pMonsterComp->monsterId.empty() )
						pMonsterComp->monsterId = prefabOrId;
				}

				pMinion->addTag( "Monster"_tag );
			}
		};

		pGameObjectManager->executeOrDeferPostTick( spawnAll );
	}
} // namespace sw
