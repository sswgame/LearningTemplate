#include "pch.h"

#include "Games/Demo/Actors/MonsterComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "GameFramework/Data/MonsterDataCatalog.h"
#include "GameFramework/Data/UnitStatsComponent.h"
#include "GameFramework/Kits/ActionCombat/ProjectileComponent.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	namespace
	{
		void queueProjectileAttack( GameObjectManager* pGameObjectManager, const string& projectilePrefab, const float3& selfPos, float32 dirX )
		{
			if ( pGameObjectManager == nullptr || projectilePrefab.empty() )
				return;

			const string  prefabPath = projectilePrefab;
			const float3  spawnPos	 = selfPos;
			const float32 dir		 = dirX >= 0.0f ? 1.0f : -1.0f;

			auto spawnProjectile = [pGameObjectManager, prefabPath, spawnPos, dir]()
			{
				GameObject* pProjObj = nullptr;
				if ( game::areGameServicesBound() )
				{
					pProjObj = game::getService<ResourceManager>()->getPrefabManager().spawn(
						pGameObjectManager, prefabPath, "MonsterProjectile" );
				}

				if ( pProjObj == nullptr )
				{
					pProjObj = pGameObjectManager->createGameObject( hashed_string( "MonsterProjectile" ) );
					if ( pProjObj == nullptr )
						return;

					SceneComponent* pProjSceneComp = pProjObj->addComponent<SceneComponent>();
					if ( pProjSceneComp != nullptr )
						pProjSceneComp->setLocalPosition( spawnPos );

					ProjectileComponent* pProjComp = pProjObj->addComponent<ProjectileComponent>();
					if ( pProjComp != nullptr )
					{
						ProjectileData* pProjData = pProjComp->ensureProjectileData();
						if ( pProjData != nullptr )
						{
							pProjData->velocity = float2{ dir * 300.0f, 0.0f };
							pProjData->damage	= 10;
							pProjData->lifeTime = 3.0f;
						}
					}
					pProjObj->addTag( "Bullet"_tag );
					return;
				}

				SceneComponent* pProjSceneComp = pProjObj->getPrimarySceneComponent();
				if ( pProjSceneComp != nullptr )
					pProjSceneComp->setLocalPosition( spawnPos );

				ProjectileComponent* pProjComp = pProjObj->getComponent<ProjectileComponent>().get();
				if ( pProjComp == nullptr )
					pProjComp = pProjObj->addComponent<ProjectileComponent>();
				if ( pProjComp != nullptr )
				{
					ProjectileData* pProjData = pProjComp->ensureProjectileData();
					if ( pProjData != nullptr )
					{
						pProjData->velocity = float2{ dir * 300.0f, 0.0f };
						pProjData->damage	= 10;
						pProjData->lifeTime = 3.0f;
					}
				}
				pProjObj->addTag( "Bullet"_tag );
			};

			pGameObjectManager->executeOrDeferPostTick( spawnProjectile );
		}
	} // namespace

	MonsterComponent::MonsterComponent()
		: archetype{ MonsterArchetype::MeleePatrol }
		, patrolRange{ 0.0f }
		, detectRange{ 0.0f }
		, attackRange{ 0.0f }
		, moveSpeed{ 0.0f }
		, attackCoolTime{ 0.0f }
		, moveDir{ 1 }
		, stateTimer{ 0.0f }
		, aiState{ MonsterAiState::Patrol }
		, startX{ 0.0f }
		, attackTimer{ 0.0f }
	{
	}

	void MonsterComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
		{
			pOwner->addTag( "Monster"_tag );
			SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
			if ( pSceneComp != nullptr )
				startX = pSceneComp->getLocalPosition()._x;
		}

		if ( monsterId.empty() == false )
		{
			const MonsterDef* pDef = MonsterDataCatalog::findMonster( monsterId );
			if ( pDef != nullptr )
			{
				archetype		 = pDef->_archetype;
				moveSpeed		 = pDef->_speed;
				attackRange		 = pDef->_attackRange;
				attackCoolTime	 = pDef->_attackCoolTime;
				projectilePrefab = pDef->_projectilePrefab;

				GameObject* pCurrentOwner = getOwner();
				if ( pCurrentOwner != nullptr )
				{
					UnitStatsComponent* pStats = pCurrentOwner->getComponent<UnitStatsComponent>().get();
					if ( pStats != nullptr )
					{
						UnitStatsData* pData = pStats->ensureStatsData();
						if ( pData != nullptr )
						{
							pData->hp					= pDef->_hp;
							pData->maxHp				= pDef->_maxHp;
							pData->attack				= pDef->_atk;
							pData->defense				= pDef->_def;
							pData->moveSpeed			= pDef->_speed;
							pData->maxInvincibilityTime = pDef->_invincibility;
						}
					}
				}
			}
		}

		stateTimer	= 0.0f;
		attackTimer = 0.0f;
		aiState		= MonsterAiState::Patrol;
	}

	void MonsterComponent::onEndPlay()
	{
	}

	void MonsterComponent::onTick( float32 deltaTime )
	{
		updateAI( deltaTime );
	}

	void MonsterComponent::updateAI( float32 deltaTime )
	{
		stateTimer += deltaTime;
		attackTimer += deltaTime;

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		float3 selfPos = pSceneComp->getLocalPosition();

		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		GameObject* pPlayerObj = pGameObjectManager->findGameObjectByTag( "Player"_tag );

		if ( pPlayerObj != nullptr && pPlayerObj->getPrimarySceneComponent() != nullptr )
		{
			const float3  playerPos = pPlayerObj->getPrimarySceneComponent()->getWorldPosition();
			const float32 distX		= playerPos._x - selfPos._x;
			const float32 dist		= MathUtil::abs( distX );

			if ( dist <= attackRange )
			{
				aiState = MonsterAiState::Attack;
				if ( attackTimer >= attackCoolTime )
				{
					attackTimer = 0.0f;
					if ( projectilePrefab.empty() == false )
					{
						queueProjectileAttack( pGameObjectManager, projectilePrefab, selfPos, distX );
					}
					else
					{
						int32				attackDamage = 10;
						UnitStatsComponent* pSelfStats	 = pOwner->getComponent<UnitStatsComponent>().get();
						if ( pSelfStats != nullptr )
						{
							UnitStatsData* pSelfData = pSelfStats->getStatsData();
							if ( pSelfData != nullptr )
								attackDamage = pSelfData->attack;
						}

						UnitStatsComponent* pPlayerStats = pPlayerObj->getComponent<UnitStatsComponent>().get();
						if ( pPlayerStats != nullptr )
							pPlayerStats->takeDamage( attackDamage );
					}
				}
				return;
			}
			if ( dist <= detectRange )
			{
				aiState = MonsterAiState::Chase;
				moveDir = ( distX >= 0.0f ) ? 1 : -1;
				selfPos._x += static_cast<float32>( moveDir ) * moveSpeed * deltaTime;
				pSceneComp->setLocalPosition( selfPos );
				return;
			}
		}

		aiState = MonsterAiState::Patrol;
		if ( selfPos._x >= startX + patrolRange )
			moveDir = -1;
		else if ( selfPos._x <= startX - patrolRange )
			moveDir = 1;

		selfPos._x += static_cast<float32>( moveDir ) * moveSpeed * deltaTime;
		pSceneComp->setLocalPosition( selfPos );
	}
} // namespace sw
