#include "pch.h"

#include "GameFramework/Data/UnitStatsComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
	namespace
	{
		void enqueueStatsMutation( GameObject* pOwner, int32 amount, bool bHeal )
		{
			if ( pOwner == nullptr )
				return;

			GameObjectManager* pManager = pOwner->getManager();
			if ( pManager == nullptr )
				return;

			const uint64 objectId = pOwner->getObjectId();
			pManager->deferPostTick( [pManager, objectId, amount, bHeal]()
			{
				GameObject* pObj = pManager->findGameObjectById( objectId );
				if ( pObj == nullptr )
					return;

				UnitStatsComponent* pStats = pObj->getComponent<UnitStatsComponent>();
				if ( pStats == nullptr )
					return;

				if ( bHeal )
					pStats->heal( amount );
				else
					pStats->takeDamage( amount );
			} );
		}
	} // namespace

	UnitStatsComponent::UnitStatsComponent()
		: _hp{ 0 }
		, _maxHp{ 0 }
		, _attack{ 0 }
		, _defense{ 0 }
		, _moveSpeed{ 0.0f }
		, _invincibilityTime{ 0.0f }
		, _maxInvincibilityTime{ 0.0f }
		, _bIsDead{ false }
	{
	}

	void UnitStatsComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Stats"_tag );
	}

	void UnitStatsComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void UnitStatsComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		if ( _invincibilityTime > 0.0f )
		{
			_invincibilityTime -= deltaTime;
			if ( _invincibilityTime < 0.0f )
				_invincibilityTime = 0.0f;
		}
	}

	int32 UnitStatsComponent::getHp() const
	{
		return _hp;
	}

	int32 UnitStatsComponent::getMaxHp() const
	{
		return _maxHp;
	}

	int32 UnitStatsComponent::getAttack() const
	{
		return _attack;
	}

	int32 UnitStatsComponent::getDefense() const
	{
		return _defense;
	}

	float32 UnitStatsComponent::getMoveSpeed() const
	{
		return _moveSpeed;
	}

	bool UnitStatsComponent::isDead() const
	{
		return _bIsDead;
	}

	void UnitStatsComponent::setStats( int32 hp, int32 maxHp, int32 attack, int32 defense, float32 moveSpeed, float32 maxInvincibilityTime )
	{
		_hp					  = hp;
		_maxHp				  = maxHp;
		_attack				  = attack;
		_defense			  = defense;
		_moveSpeed			  = moveSpeed;
		_maxInvincibilityTime = maxInvincibilityTime;
	}

	void UnitStatsComponent::takeDamage( int32 amount )
	{
		GameObject*		   pOwner	= getOwner();
		GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
		if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
		{
			enqueueStatsMutation( pOwner, amount, false );
			return;
		}
		applyTakeDamage( amount );
	}

	void UnitStatsComponent::heal( int32 amount )
	{
		GameObject*		   pOwner	= getOwner();
		GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
		if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
		{
			enqueueStatsMutation( pOwner, amount, true );
			return;
		}
		applyHeal( amount );
	}

	void UnitStatsComponent::applyTakeDamage( int32 amount )
	{
		const bool bCannotTakeDamage = ( _bIsDead || _invincibilityTime > 0.0f );
		if ( bCannotTakeDamage )
			return;

		int32 actualDamage = amount - _defense;
		if ( actualDamage < 1 )
			actualDamage = 1;

		_hp -= actualDamage;
		if ( _hp <= 0 )
		{
			_hp		 = 0;
			_bIsDead = true;
		}
		else
			_invincibilityTime = _maxInvincibilityTime;
	}

	void UnitStatsComponent::applyHeal( int32 amount )
	{
		if ( _bIsDead )
			return;

		_hp += amount;
		if ( _hp > _maxHp )
			_hp = _maxHp;
	}
} // namespace sw
