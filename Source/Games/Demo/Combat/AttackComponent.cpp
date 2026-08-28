#include "pch.h"

#include "Games/Demo/Combat/AttackComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Data/UnitStatsComponent.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	AttackComponent::AttackComponent()
		: _attackKind{ AttackKind::MeleeSweep }
		, _knockbackPower{ 0.0f }
		, _hitboxSize{ 64.0f, 64.0f }
		, _listHitVictim{}
	{
	}

	void AttackComponent::onBeginPlay()
	{
		AttackBaseComponent::onBeginPlay();
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Attack"_tag );
		_listHitVictim.clear();
	}

	void AttackComponent::onEndPlay()
	{
		AttackBaseComponent::onEndPlay();
	}

	void AttackComponent::onTick( float32 deltaTime )
	{
		AttackBaseComponent::onTick( deltaTime );

		if ( isAttackActive() == false )
		{
			_listHitVictim.clear();
			return;
		}

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

		const float3 ownerWorldPos = pOwnerSceneComp->getWorldPosition();
		const float2 halfHitbox{ _hitboxSize._x * 0.5f, _hitboxSize._y * 0.5f };
		const float2 attackMin{ ownerWorldPos._x - halfHitbox._x, ownerWorldPos._y - halfHitbox._y };
		const float2 attackMax{ ownerWorldPos._x + halfHitbox._x, ownerWorldPos._y + halfHitbox._y };

		const bool	bPlayerAttacking = pOwner->hasTag( "Player"_tag );
		const TagID targetTag		 = bPlayerAttacking ? "Monster"_tag : "Player"_tag;

		vector<GameObject*> targetList;
		pGameObjectManager->findGameObjectsByTag( targetTag, targetList );
		for ( GameObject* pTargetObj : targetList )
		{
			if ( pTargetObj == nullptr || pTargetObj == pOwner )
				continue;

			const uint64 targetId = pTargetObj->getObjectId();
			if ( std::find( _listHitVictim.begin(), _listHitVictim.end(), targetId ) != _listHitVictim.end() )
				continue;

			SceneComponent* pTargetSceneComp = pTargetObj->getPrimarySceneComponent();
			if ( pTargetSceneComp == nullptr )
				continue;

			const float3 targetWorldPos = pTargetSceneComp->getWorldPosition();
			const bool	 bInX			= ( targetWorldPos._x >= attackMin._x && targetWorldPos._x <= attackMax._x );
			const bool	 bInY			= ( targetWorldPos._y >= attackMin._y && targetWorldPos._y <= attackMax._y );

			if ( bInX && bInY )
			{
				UnitStatsComponent* pStats = pTargetObj->getComponent<UnitStatsComponent>();
				if ( pStats != nullptr )
				{
					const int32 damageAmount = getDamage() > 0 ? getDamage() : 10;
					pStats->takeDamage( damageAmount );
					_listHitVictim.push_back( targetId );

					if ( _knockbackPower > 0.0f )
					{
						const float32 dirX	 = ( targetWorldPos._x >= ownerWorldPos._x ) ? 1.0f : -1.0f;
						float3		  newPos = pTargetSceneComp->getLocalPosition();
						newPos._x += dirX * ( _knockbackPower * 0.1f );
						pTargetSceneComp->setLocalPosition( newPos );
					}
				}
			}
		}
	}

	void AttackComponent::triggerAttack( AttackKind kind, float32 damage, float32 duration, const float2& size )
	{
		_attackKind = kind;
		beginAttack( static_cast<int32>( damage ), duration );
		_hitboxSize = size;
		_listHitVictim.clear();
	}
} // namespace sw
