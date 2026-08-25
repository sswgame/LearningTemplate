#include "pch.h"

#include "GameFramework/UI/DamageUIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/EcsDataUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void DamageUIComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PostUpdate );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "UI"_tag );

		DamageUIData* pData = ensureDamageUIData();
		if ( pData != nullptr )
		{
			pData->currentLife = 0.0f;
			pData->alpha	   = 1.0f;
		}
	}

	void DamageUIComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void DamageUIComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		DamageUIData* pData = ensureDamageUIData();
		if ( pData == nullptr )
			return;

		pData->currentLife += deltaTime;
		if ( pData->lifeTime > 0.0f )
		{
			pData->alpha	   = MathUtil::clamp( 1.0f - ( pData->currentLife / pData->lifeTime ), 0.0f, 1.0f );
			GameObject* pOwner = getOwner();
			if ( pOwner != nullptr )
			{
				if ( pData->currentLife >= pData->lifeTime )
				{
					pOwner->markPendingKill();
					return;
				}
				SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
				if ( pSceneComp != nullptr )
				{
					float3 pos = pSceneComp->getLocalPosition();
					pos._y += pData->floatSpeed * deltaTime;
					pSceneComp->setLocalPosition( pos );
				}
			}
		}
	}

	Component::EcsDataView DamageUIComponent::ensureEcsData()
	{
		DamageUIData* pData = ensureDamageUIData();
		return { pData, DamageUIData::StaticType() };
	}

	Component::EcsDataView DamageUIComponent::getEcsData() const
	{
		return { ( getDamageUIData() ), DamageUIData::StaticType() };
	}

	DamageUIData* DamageUIComponent::getDamageUIData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			return pOwner->getComponent<DamageUIData>().get();
		return nullptr;
	}

	DamageUIData* DamageUIComponent::ensureDamageUIData()
	{
		return sw::ensureEcsData<DamageUIData>( getOwner(), getTypeInfo() );
	}
} // namespace sw
