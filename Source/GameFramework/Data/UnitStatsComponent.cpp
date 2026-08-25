#include "pch.h"

#include "GameFramework/Data/UnitStatsComponent.h"

#include "Engine/Object/Component/EcsDataUtil.h"
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

				UnitStatsComponent* pStats = pObj->getComponent<UnitStatsComponent>().get();
				if ( pStats == nullptr )
					return;

				if ( bHeal )
					pStats->heal( amount );
				else
					pStats->takeDamage( amount );
			} );
		}
	} // namespace

	void UnitStatsComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Stats"_tag );
		ensureStatsData();
	}

	void UnitStatsComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void UnitStatsComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		UnitStatsData* pData = ensureStatsData();
		if ( pData == nullptr )
			return;

		if ( pData->invincibilityTime > 0.0f )
		{
			pData->invincibilityTime -= deltaTime;
			if ( pData->invincibilityTime < 0.0f )
				pData->invincibilityTime = 0.0f;
		}
	}

	Component::EcsDataView UnitStatsComponent::ensureEcsData()
	{
		UnitStatsData* pData = ensureStatsData();
		return { pData, UnitStatsData::StaticType() };
	}

	Component::EcsDataView UnitStatsComponent::getEcsData() const
	{
		return { getStatsData(), UnitStatsData::StaticType() };
	}

	UnitStatsData* UnitStatsComponent::getStatsData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			return pOwner->getComponent<UnitStatsData>().get();
		return nullptr;
	}

	UnitStatsData* UnitStatsComponent::ensureStatsData()
	{
		return sw::ensureEcsData<UnitStatsData>( getOwner(), getTypeInfo() );
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
		UnitStatsData* pData = ensureStatsData();
		if ( pData == nullptr )
			return;

		const bool bCannotTakeDamage = ( pData->bIsDead || pData->invincibilityTime > 0.0f );
		if ( bCannotTakeDamage )
			return;

		int32 actualDamage = amount - pData->defense;
		if ( actualDamage < 1 )
			actualDamage = 1;

		pData->hp -= actualDamage;
		if ( pData->hp <= 0 )
		{
			pData->hp	   = 0;
			pData->bIsDead = true;
		}
		else
		{
			pData->invincibilityTime = pData->maxInvincibilityTime;
		}
	}

	void UnitStatsComponent::applyHeal( int32 amount )
	{
		UnitStatsData* pData = ensureStatsData();
		if ( pData == nullptr || pData->bIsDead )
			return;

		pData->hp += amount;
		if ( pData->hp > pData->maxHp )
			pData->hp = pData->maxHp;
	}
} // namespace sw
