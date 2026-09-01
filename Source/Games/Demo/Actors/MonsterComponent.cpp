#include "pch.h"

#include "Games/Demo/Actors/MonsterComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceManager.h"

#include "GameFramework/Base/GameService.h"
#include "GameFramework/Data/MonsterDataCatalog.h"
#include "GameFramework/Data/UnitStatsComponent.h"
#include "GameFramework/Kits/ActionCombat/ProjectileComponent.h"

namespace sw
{
	namespace
	{
		struct MonsterComponentInternal
		{
			static void queueProjectileAttack( GameObjectManager* pGameObjectManager, const string& projectilePrefab, const float3& selfPos, float32 dirX )
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
							pProjComp->setVelocity( float2{ dir * 300.0f, 0.0f } );
							pProjComp->setDamage( 10 );
							pProjComp->setLifeTime( 3.0f );
						}
						pProjObj->addTag( "Bullet"_tag );
						return;
					}

					SceneComponent* pProjSceneComp = pProjObj->getPrimarySceneComponent();
					if ( pProjSceneComp != nullptr )
						pProjSceneComp->setLocalPosition( spawnPos );

					ProjectileComponent* pProjComp = pProjObj->getComponent<ProjectileComponent>();
					if ( pProjComp == nullptr )
						pProjComp = pProjObj->addComponent<ProjectileComponent>();
					if ( pProjComp != nullptr )
					{
						pProjComp->setVelocity( float2{ dir * 300.0f, 0.0f } );
						pProjComp->setDamage( 10 );
						pProjComp->setLifeTime( 3.0f );
					}
					pProjObj->addTag( "Bullet"_tag );
				};

				pGameObjectManager->executeOrDeferPostTick( spawnProjectile );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	MonsterComponent::MonsterComponent()
		: _archetype{ MonsterArchetype::MeleePatrol }
		, _patrolRange{ 0.0f }
		, _detectRange{ 0.0f }
		, _attackRange{ 0.0f }
		, _moveSpeed{ 0.0f }
		, _attackCoolTime{ 0.0f }
		, _moveDir{ 1 }
		, _stateTimer{ 0.0f }
		, _aiState{ MonsterAiState::Patrol }
		, _startX{ 0.0f }
		, _attackTimer{ 0.0f }
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
				_startX = pSceneComp->getLocalPosition()._x;
		}

		if ( _monsterId.empty() == false )
		{
			const MonsterDataCatalog* pCatalog = game::getService<MonsterDataCatalog>();
			const MonsterDef*		  pDef	   = pCatalog != nullptr ? pCatalog->findMonster( _monsterId ) : nullptr;
			if ( pDef != nullptr )
			{
				_archetype		  = pDef->_archetype;
				_moveSpeed		  = pDef->_speed;
				_attackRange	  = pDef->_attackRange;
				_attackCoolTime	  = pDef->_attackCoolTime;
				_projectilePrefab = pDef->_projectilePrefab;

				GameObject* pCurrentOwner = getOwner();
				if ( pCurrentOwner != nullptr )
				{
					UnitStatsComponent* pStats = pCurrentOwner->getComponent<UnitStatsComponent>();
					if ( pStats != nullptr )
					{
						pStats->setStats( pDef->_hp, pDef->_maxHp, pDef->_atk, pDef->_def, pDef->_speed, pDef->_invincibility );
					}
				}
			}
		}

		_stateTimer	 = 0.0f;
		_attackTimer = 0.0f;
		_aiState	 = MonsterAiState::Patrol;
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
		_stateTimer += deltaTime;
		_attackTimer += deltaTime;

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

			if ( dist <= _attackRange )
			{
				_aiState = MonsterAiState::Attack;
				if ( _attackTimer >= _attackCoolTime )
				{
					_attackTimer = 0.0f;
					if ( _projectilePrefab.empty() == false )
					{
						MonsterComponentInternal::queueProjectileAttack( pGameObjectManager, _projectilePrefab, selfPos, distX );
					}
					else
					{
						int32				attackDamage = 10;
						UnitStatsComponent* pSelfStats	 = pOwner->getComponent<UnitStatsComponent>();
						if ( pSelfStats != nullptr )
							attackDamage = pSelfStats->getAttack();

						UnitStatsComponent* pPlayerStats = pPlayerObj->getComponent<UnitStatsComponent>();
						if ( pPlayerStats != nullptr )
							pPlayerStats->takeDamage( attackDamage );
					}
				}
				return;
			}
			if ( dist <= _detectRange )
			{
				_aiState = MonsterAiState::Chase;
				_moveDir = ( distX >= 0.0f ) ? 1 : -1;
				selfPos._x += static_cast<float32>( _moveDir ) * _moveSpeed * deltaTime;
				pSceneComp->setLocalPosition( selfPos );
				return;
			}
		}

		_aiState = MonsterAiState::Patrol;
		if ( selfPos._x >= _startX + _patrolRange )
			_moveDir = -1;
		else if ( selfPos._x <= _startX - _patrolRange )
			_moveDir = 1;

		selfPos._x += static_cast<float32>( _moveDir ) * _moveSpeed * deltaTime;
		pSceneComp->setLocalPosition( selfPos );
	}
} // namespace sw
