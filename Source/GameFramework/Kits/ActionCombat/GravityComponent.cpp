#include "pch.h"

#include "GameFramework/Kits/ActionCombat/GravityComponent.h"

#include "Engine/Object/Component/EcsDataUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void GravityComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Physics"_tag );
		ensureGravityData();
	}

	void GravityComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void GravityComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		GravityData* pData = ensureGravityData();
		if ( pData == nullptr )
			return;

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
		{
			SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
			if ( pSceneComp != nullptr )
			{
				float3 pos = pSceneComp->getLocalPosition();
				if ( pData->bIsGrounded == false )
				{
					pData->velocityY += pData->gravity * deltaTime;
					pos._y += pData->velocityY * deltaTime;
					if ( pos._y <= pData->groundY )
					{
						pos._y			   = pData->groundY;
						pData->velocityY   = 0.0f;
						pData->bIsGrounded = true;
					}
				}
				pSceneComp->setLocalPosition( pos );
			}
		}
	}

	Component::EcsDataView GravityComponent::ensureEcsData()
	{
		GravityData* pData = ensureGravityData();
		return { pData, GravityData::StaticType() };
	}

	Component::EcsDataView GravityComponent::getEcsData() const
	{
		return { ( getGravityData() ), GravityData::StaticType() };
	}

	GravityData* GravityComponent::getGravityData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			return pOwner->getComponent<GravityData>().get();
		return nullptr;
	}

	GravityData* GravityComponent::ensureGravityData()
	{
		return sw::ensureEcsData<GravityData>( getOwner(), getTypeInfo() );
	}
} // namespace sw
