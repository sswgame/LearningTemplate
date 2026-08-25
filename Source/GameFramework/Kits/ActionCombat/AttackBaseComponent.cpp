#include "pch.h"

#include "GameFramework/Kits/ActionCombat/AttackBaseComponent.h"

#include "Engine/Object/Component/EcsDataUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void AttackBaseComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Combat"_tag );

		AttackBaseData* pData = ensureAttackData();
		if ( pData != nullptr )
			pData->currentDuration = 0.0f;
	}

	void AttackBaseComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void AttackBaseComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		AttackBaseData* pData = ensureAttackData();
		if ( pData == nullptr )
			return;

		if ( pData->bActive )
		{
			pData->currentDuration += deltaTime;
			if ( pData->duration > 0.0f && pData->currentDuration >= pData->duration )
			{
				pData->bActive		   = false;
				pData->currentDuration = 0.0f;
			}
		}
	}

	Component::EcsDataView AttackBaseComponent::ensureEcsData()
	{
		AttackBaseData* pData = ensureAttackData();
		return { pData, AttackBaseData::StaticType() };
	}

	Component::EcsDataView AttackBaseComponent::getEcsData() const
	{
		return { ( getAttackData() ), AttackBaseData::StaticType() };
	}

	AttackBaseData* AttackBaseComponent::getAttackData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			return pOwner->getComponent<AttackBaseData>().get();
		return nullptr;
	}

	AttackBaseData* AttackBaseComponent::ensureAttackData()
	{
		return sw::ensureEcsData<AttackBaseData>( getOwner(), getTypeInfo() );
	}
} // namespace sw
