#include "pch.h"

#include "GameFramework/Base/EffectBaseComponent.h"

#include "Engine/Object/Component/EcsDataUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void EffectBaseComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PostPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "VFX"_tag );

		EffectBaseData* pData = ensureEffectData();
		if ( pData != nullptr )
		{
			pData->currentTimer = 0.0f;
			pData->currentAlpha = 1.0f;
		}
	}

	void EffectBaseComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void EffectBaseComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		EffectBaseData* pData = ensureEffectData();
		if ( pData == nullptr )
			return;

		pData->currentTimer += deltaTime;
		if ( pData->duration > 0.0f )
		{
			pData->currentAlpha = 1.0f - ( pData->currentTimer / pData->duration );
			if ( pData->currentAlpha < 0.0f )
			{
				pData->currentAlpha = 0.0f;
				GameObject* pOwner	= getOwner();
				if ( pOwner != nullptr )
					pOwner->markPendingKill();
			}
		}
	}

	Component::EcsDataView EffectBaseComponent::ensureEcsData()
	{
		EffectBaseData* pData = ensureEffectData();
		return { pData, EffectBaseData::StaticType() };
	}

	Component::EcsDataView EffectBaseComponent::getEcsData() const
	{
		return { ( getEffectData() ), EffectBaseData::StaticType() };
	}

	EffectBaseData* EffectBaseComponent::getEffectData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			return pOwner->getComponent<EffectBaseData>().get();
		return nullptr;
	}

	EffectBaseData* EffectBaseComponent::ensureEffectData()
	{
		return sw::ensureEcsData<EffectBaseData>( getOwner(), getTypeInfo() );
	}
} // namespace sw
